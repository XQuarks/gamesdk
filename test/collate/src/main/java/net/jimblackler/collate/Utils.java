package net.jimblackler.collate;

import java.io.BufferedReader;
import java.io.IOException;
import java.io.InputStream;
import java.io.InputStreamReader;
import java.io.OutputStream;
import java.io.PrintStream;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.util.ArrayList;
import java.util.Collection;
import java.util.HashMap;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;
import java.util.stream.Stream;

/**
 * Class containing various utility methods used by tools in the package.
 */
class Utils {
  private static final PrintStream NULL_PRINT_STREAM = new PrintStream(new OutputStream() {
    @Override
    public void write(int b) {}
  });

  static String fileToString(String filename) throws IOException {
    return Files.readString(Paths.get("resources", filename));
  }

  public static void copyFolder(Path from, Path to) throws IOException {
    Files.createDirectory(to);
    try (Stream<Path> stream = Files.walk(from)) {
      stream.forEachOrdered(path -> {
        if (Files.isDirectory(path)) {
          return;
        }
        try {
          Files.copy(path, to.resolve(from.relativize(path)));
        } catch (IOException e) {
          throw new IllegalStateException(e);
        }
      });
    }
  }

  private static String readStream(InputStream inputStream, PrintStream out) throws IOException {
    StringBuilder stringBuilder = new StringBuilder();
    try (BufferedReader bufferedReader =
             new BufferedReader(new InputStreamReader(inputStream, StandardCharsets.UTF_8))) {
      String separator = "";
      while (true) {
        String line = bufferedReader.readLine();
        if (line == null) {
          return stringBuilder.toString();
        }
        out.println(line);
        stringBuilder.append(separator);
        separator = System.lineSeparator();
        stringBuilder.append(line);
      }
    }
  }

  static String executeSilent(String... args) throws IOException {
    return execute(NULL_PRINT_STREAM, args);
  }

  static String execute(String... args) throws IOException {
    return execute(System.out, args);
  }

  static String execute(PrintStream out, String... args) throws IOException {
    out.println(String.join(" ", args));
    int sleepFor = 0;
    for (int attempt = 0; attempt != 10; attempt++) {
      Process process = new ProcessBuilder(args).start();

      String input = readStream(process.getInputStream(), out);
      String error = readStream(process.getErrorStream(), out);
      try {
        process.waitFor();
      } catch (InterruptedException e) {
        throw new RuntimeException(e);
      }
      int exit = process.exitValue();
      if (exit != 0) {
        if (error.contains("Broken pipe") || error.contains("Can't find service")) {
          sleepFor += 10;
          out.println(error);
          out.print("Retrying in " + sleepFor + " seconds... ");
          try {
            Thread.sleep(sleepFor * 1000L);
          } catch (InterruptedException e) {
            // Intentionally ignored.
          }
          out.println("done");
          continue;
        }
        throw new IOException(error);
      }
      return input;
    }
    throw new IOException("Maximum attempts reached");
  }

  /**
   * Selects the parameters for a run based on the 'tests' and 'coordinates' of the test
   * specification file.
   * @param spec The test specification file.
   * @return The selected parameters.
   */
  static Map<String, Object> flattenParams(Map<String, Object> spec) {
    Map<String, Object> params = new HashMap<>();

    List<Object> coordinates = (List<Object>) spec.get("coordinates");
    List<Object> tests = (List<Object>) spec.get("tests");

    for (int coordinateNumber = 0; coordinateNumber != coordinates.size(); coordinateNumber++) {
      List<Object> jsonArray = (List<Object>) tests.get(coordinateNumber);
      Map<String, Object> jsonObject = (Map<String, Object>) jsonArray.get(
          ((Number) coordinates.get(coordinateNumber)).intValue());
      merge(jsonObject, params);
    }
    return params;
  }

  /**
   * Creates a deep union of two maps. Arrays are concatenated and dictionaries are merged.
   * @param in The first map; read only.
   * @param out The second map and the object into which changes are written.
   */
  private static void merge(Map<String, Object> in, Map<String, Object> out) {
    in = clone(in);
    for (Map.Entry<String, Object> entry : in.entrySet()) {
      String key = entry.getKey();
      Object inObject = entry.getValue();
      if (out.containsKey(key)) {
        Object outObject = out.get(key);
        if (inObject instanceof List && outObject instanceof List) {
          ((Collection<Object>) outObject).addAll((Collection<?>) inObject);
          continue;
        }
        if (inObject instanceof Map && outObject instanceof Map) {
          merge((Map<String, Object>) inObject, ((Map<String, Object>) outObject));
          continue;
        }
      }
      out.put(key, inObject);
    }
  }

  /**
   * Return a deep-clone of a JSON-compatible object tree.
   *
   * @param in  The tree to clone.
   * @param <T> The cloned tree.
   */
  public static <T> T clone(T in) {
    if (in instanceof Map) {
      Map<String, Object> map = new LinkedHashMap<>();
      for (Map.Entry<String, Object> entry : ((Map<String, Object>) in).entrySet()) {
        map.put(entry.getKey(), clone(entry.getValue()));
      }
      return (T) map;
    }

    if (in instanceof List) {
      Collection<Object> list = new ArrayList<>();
      for (Object obj : (Iterable<Object>) in) {
        list.add(clone(obj));
      }
      return (T) list;
    }
    return in;
  }

  static String getProjectId() throws IOException {
    return execute(Config.GCLOUD_EXECUTABLE.toString(), "config", "get-value", "project");
  }

  /**
   * Converts a time duration in an object to a number of milliseconds. If the object is a number,
   * it is interpreted as the number of milliseconds. If the value is a string, it is converted
   * according to the specified unit. e.g. "30s", "1H". No unit is interpreted as milliseconds.
   *
   * @param object The object to extract from.
   * @return The equivalent number of milliseconds.
   */
  static long getDuration(Object object) {
    if (object instanceof Number) {
      return ((Number) object).longValue();
    }

    if (object instanceof String) {
      String str = ((String) object).toUpperCase();
      int unitPosition = str.indexOf('S');
      int unitMultiplier = 1000;
      if (unitPosition == -1) {
        unitPosition = str.indexOf('M');
        unitMultiplier *= 60;
        if (unitPosition == -1) {
          unitPosition = str.indexOf('H');
          unitMultiplier *= 60;
          if (unitPosition == -1) {
            unitMultiplier = 1;
          }
        }
      }
      float value = Float.parseFloat(str.substring(0, unitPosition));
      return (long) (value * unitMultiplier);
    }
    throw new IllegalArgumentException("Input to getDuration neither string or number.");
  }

  /**
   * Return the value associated with the key in the given map. If the map does not define
   * the key, return the specified default value.
   * @param object The map to extract the value from.
   * @param key The key associated with the value.
   * @param defaultValue The value to return if the map does not specify the key.
   * @param <T> The parameter type.
   * @return The associated value, or the defaultValue if the object does not define the key.
   */
  public static <T> T getOrDefault(Map<String, Object> object, String key, T defaultValue) {
    if (object.containsKey(key)) {
      return (T) object.get(key);
    }
    return defaultValue;
  }
}
