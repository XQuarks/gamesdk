package com.google.android.apps.internal.games.memoryadvice;

import android.app.Service;
import android.content.Intent;
import android.os.Bundle;
import android.os.Handler;
import android.os.IBinder;
import android.os.Message;
import android.os.Messenger;
import android.os.RemoteException;
import android.util.Log;
import com.fasterxml.jackson.databind.ObjectMapper;
import com.fasterxml.jackson.databind.ObjectReader;
import java.io.IOException;
import java.util.LinkedHashMap;
import java.util.Map;

/**
 * The service component of the on device stress test.
 */
public class StressService extends Service {
  private static final String TAG = StressService.class.getSimpleName();
  private final int pid = android.os.Process.myPid();

  private Handler messageHandler;
  private MemoryMonitor memoryMonitor;
  private long applicationAllocated;

  @Override
  public int onStartCommand(Intent intent, int flags, int startId) {
    ObjectReader objectReader = new ObjectMapper().reader();

    // Create a local memory monitor, to obtain the metrics.
    Map<String, Object> params = null;
    try {
      params = objectReader.readValue(intent.getStringExtra("params"), Map.class);
    } catch (IOException e) {
      throw new IllegalStateException(e);
    }
    Map<String, Object> metrics = (Map<String, Object>) params.get("metrics");
    memoryMonitor = new MemoryMonitor(this, metrics);

    return START_NOT_STICKY;
  }

  @Override
  public IBinder onBind(Intent intent) {
    // Handle messages from the main process.
    messageHandler = new Handler(new Handler.Callback() {
      @Override
      public boolean handleMessage(Message msg) {
        int what = msg.what;
        switch (what) {
          case OnDeviceStressTester.GET_BASELINE_METRICS:
            try {
              Message message = Message.obtain(
                  messageHandler, OnDeviceStressTester.GET_BASELINE_METRICS_RETURN, pid, 0);
              Bundle bundle = new Bundle();
              // Metrics before any allocations are are the baseline.
              Map<String, Object> metrics = memoryMonitor.getMemoryMetrics();
              bundle.putString("metrics", metrics.toString());
              message.obj = bundle;
              msg.replyTo.send(message);
            } catch (RemoteException e) {
              Log.w(TAG, "Error sending message", e);
              stopSelf();
            }
            break;
          case OnDeviceStressTester.OCCUPY_MEMORY:
            // This allocates memory until allocations failed, or the process is killed by the
            // system.
            int toAllocate = msg.arg1;
            int returnCode;

            Log.i(TAG, "Allocating " + toAllocate);
            if (TryAllocTester.occupyMemory(toAllocate)) {
              applicationAllocated += toAllocate;
              returnCode = OnDeviceStressTester.OCCUPY_MEMORY_OK;
            } else {
              returnCode = OnDeviceStressTester.OCCUPY_MEMORY_FAILED;
            }

            // Memory metrics are calculated and returned to the main process.
            Map<String, Object> metrics = memoryMonitor.getMemoryMetrics();

            // This format matches the format used by the lab stress tester.
            Map<String, Object> stressed = new LinkedHashMap<>();
            stressed.put("applicationAllocated", applicationAllocated);
            metrics.put("stressed", stressed);

            Bundle bundle = new Bundle();
            bundle.putString("metrics", metrics.toString());
            Message message = Message.obtain(messageHandler, returnCode, bundle);
            try {
              msg.replyTo.send(message);
            } catch (RemoteException e) {
              Log.w(TAG, "Error sending message: closing service", e);
              stopSelf();
              break;
            }

            break;
          default:
            throw new MemoryAdvisorException("Unknown command");
        }
        return true;
      }
    });
    return new Messenger(messageHandler).getBinder();
  }
}
