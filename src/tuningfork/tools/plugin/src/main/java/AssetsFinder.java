/*
 * Copyright (C) 2020 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License
 */

import java.io.File;
import java.util.Arrays;
import java.util.List;
import java.util.Optional;
import java.util.regex.Pattern;
import java.util.stream.Collectors;

public class AssetsFinder {

  public static Optional<File> findAssets() {
    File currentDirectory = new File(new File(".").getAbsolutePath());
    return findAssets(currentDirectory);
  }

  public static Optional<File> findAssets(String projectPath) {
    File currentDirectory = new File(projectPath);
    return findAssets(currentDirectory);
  }

  public static Optional<File> findAssets(File currentDirectory) {
    File[] files = currentDirectory.listFiles();

    if (files.length == 0) {
      return Optional.empty();
    }

    List<File> foundDir = Arrays.stream(files)
        .filter(file -> Pattern.matches(".*/assets/tuningfork", file.getAbsolutePath()))
        .collect(Collectors.toList());

    if (foundDir.size() == 1) {
      File match = foundDir.get(0);
      if (match.isDirectory()) {
        return Optional.of(match);
      }
    } else if (foundDir.size() == 0) {
      List<Optional<File>> foundRecursive =
          Arrays.stream(files)
              .filter(File::isDirectory)
              .map(AssetsFinder::findAssets)
              .filter(Optional::isPresent)
              .collect(Collectors.toList());
      if (foundRecursive.size() > 0) {
        return foundRecursive.get(0);
      }
    }

    return Optional.empty();
  }
}