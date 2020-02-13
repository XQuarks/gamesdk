/*
 * Copyright 2019 The Android Open Source Project
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
 * limitations under the License.
 */

package com.google.gamesdk.gamecert.operationrunner.hosts;

import android.content.ComponentCallbacks2;
import android.content.Context;
import android.content.Intent;
import android.os.Bundle;
import android.os.Handler;
import android.util.Log;
import android.view.View;
import android.widget.TextView;

import androidx.annotation.Nullable;
import androidx.appcompat.app.AppCompatActivity;

import com.google.gamesdk.gamecert.operationrunner.ACTApplication;
import com.google.gamesdk.gamecert.operationrunner.MainActivity;
import com.google.gamesdk.R;
import com.google.gamesdk.gamecert.operationrunner.operations.BaseOperation;
import com.google.gamesdk.gamecert.operationrunner.operations.BaseOperation.Mode;
import com.google.gamesdk.gamecert.operationrunner.operations.Factory;
import com.google.gamesdk.gamecert.operationrunner.transport.Configuration;
import com.google.gamesdk.gamecert.operationrunner.transport.Configuration.Operation;
import com.google.gamesdk.gamecert.operationrunner.transport.Configuration.StressTest;
import com.google.gamesdk.gamecert.operationrunner.util.NativeInvoker;
import com.google.gamesdk.gamecert.operationrunner.util.SystemHelpers;

import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;
import java.util.Locale;
import java.util.stream.Collectors;

@SuppressWarnings("unused")
public class BaseHostActivity extends AppCompatActivity
        implements ComponentCallbacks2 {

    public static final String ID = "BasicHostActivity";
    private static final String TAG = BaseHostActivity.class.getSimpleName();
    protected static final String STRESS_TEST_JSON = "STRESS_TEST_JSON";
    private static final boolean SHOW_TIMING_INFO = true;

    protected boolean _canceled;
    protected boolean _running;
    protected String _suiteId;
    protected String _suiteDescription;
    protected Configuration.StressTest _stressTest;
    protected Handler _mainThreadPump;
    protected TextView _fpsTextView;
    protected TextView _minFrameTimeTextView;
    protected TextView _maxFrameTimeTextView;

    OperationWrapper _dataGatherer;
    List<OperationWrapper> _stressors = new ArrayList<>();
    List<OperationWrapper> _monitors = new ArrayList<>();

    protected SystemHelpers _systemHelpers;

    public static Intent createIntent(Context ctx, Configuration.StressTest stressTest) {
        Intent i = new Intent(ctx, BaseHostActivity.class);
        i.putExtra(STRESS_TEST_JSON, stressTest.toJson());
        return i;
    }

    @Override
    protected void onCreate(@Nullable Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        int viewResource = getContentViewResource();
        if (viewResource != 0) {
            setContentView(viewResource);
        }

        _mainThreadPump = new Handler(getMainLooper());
        _systemHelpers = new SystemHelpers(this);

        //
        // set up timing views
        //

        _fpsTextView = findViewById(R.id.fpsTextView);
        _minFrameTimeTextView = findViewById(R.id.minFrameTimeTextView);
        _maxFrameTimeTextView = findViewById(R.id.maxFrameTimeTextView);
        if (!SHOW_TIMING_INFO) {
            if (_fpsTextView != null) {
                _fpsTextView.setVisibility(View.GONE);
            }
            if (_minFrameTimeTextView != null) {
                _minFrameTimeTextView.setVisibility(View.GONE);
            }
            if (_maxFrameTimeTextView != null) {
                _maxFrameTimeTextView.setVisibility(View.GONE);
            }
        }

        //
        //  Attempt to load the operations defined in the StressTest
        //  We will attempt to load each from Java, and if not successful, then native
        //

        Intent intent = getIntent();
        String stressTestJson = intent.getStringExtra(STRESS_TEST_JSON);
        if (stressTestJson == null || stressTestJson.isEmpty()) {
            throw new IllegalArgumentException(
                    "BaseHostActivity must be launched with a " +
                            "Configuration.StressTest parameter; see createIntent");
        }
        _stressTest = Configuration.StressTest.fromJson(stressTestJson);

        _suiteId = _stressTest.getName();
        _suiteDescription = _stressTest.getDescription();
        if (getSupportActionBar() != null) {
            getSupportActionBar().setTitle(_suiteId);
        }

        NativeInvoker.initializeSuite(this, getFilesDir().getAbsolutePath(),
                                      getPackageResourcePath(),
                                      getObbDir().getAbsolutePath());

        //
        // build the data gatherers, stressors and monitors
        //

        _dataGatherer = new OperationWrapper(
                _suiteId,
                _suiteDescription,
                _stressTest.getDataGatherer(),
                this,
                BaseOperation.Mode.DATA_GATHERER);

        // we're not using java streams because of SDK 19 support
        for (Operation stressor : _stressTest.getStressors()) {
            OperationWrapper wrapper = new OperationWrapper(_suiteId, _suiteDescription, stressor,this, Mode.STRESSOR);
            _stressors.add(wrapper);
        }

        for (Operation monitor : _stressTest.getMonitors()) {
            OperationWrapper wrapper = new OperationWrapper(_suiteId, _suiteDescription, monitor, this, Mode.DATA_GATHERER);
            _monitors.add(wrapper);
        }
    }

    @Override
    protected void onResume() {
        super.onResume();
        startDataGathererAndStressors();
    }

    @Override
    public void onBackPressed() {
        _canceled = true;
        _dataGatherer.stop();
        for (OperationWrapper op : _stressors) op.stop();
    }

    @Override
    public void onTrimMemory(int level) {
        super.onTrimMemory(level);
        NativeInvoker.onTrimMemory(level);
    }

    public SystemHelpers getSystemHelpers() {
        return _systemHelpers;
    }

    public String getName() {
        return _suiteId;
    }

    public Configuration.StressTest getStressTest() {
        return _stressTest;
    }

    protected int getNativeDataGathererId() {
        return _dataGatherer.nativeHandle();
    }

    //
    // Internal
    //

    private void updateTimingStats() {
        if (_running) {
            if (_fpsTextView != null) {
                _fpsTextView.setText(
                        String.format(Locale.getDefault(),
                                "%.2f",
                                NativeInvoker.getCurrentFps()));
            }
            if (_minFrameTimeTextView != null) {
                _minFrameTimeTextView.setText(
                        String.format(Locale.getDefault(),
                                "%.3f ms",
                                NativeInvoker.getCurrentMinFrameTimeNs() / 1e6));
            }
            if (_maxFrameTimeTextView != null) {
                _maxFrameTimeTextView.setText(
                        String.format(Locale.getDefault(),
                                "%.3f ms",
                                NativeInvoker.getCurrentMaxFrameTimeNs() / 1e6));
            }
            _mainThreadPump.postDelayed(this::updateTimingStats, 1000);
        }
    }

    private void startDataGathererAndStressors() {
        Log.d(TAG, "startDataGathererAndStressors - \"" + _suiteId + "\"");
        setRunning(true);

        if (SHOW_TIMING_INFO) {
            _mainThreadPump.postDelayed(this::updateTimingStats, 1000);
        }

        _dataGatherer.start(_stressTest.getDurationMillis());

        for (OperationWrapper op : _stressors) {
            op.start(0);
        }

        for (OperationWrapper op : _monitors) {
            op.start(0);
        }

        // we want to wait for stressors and data gatherers to complete,
        // but not to block the main thread. When all threads
        // are done, we'll call stop on main thread.
        new Thread(() -> {

            _dataGatherer.waitForCompletion();

            for (OperationWrapper op : _stressors) {
                op.stop();
                op.waitForCompletion();
            }

            for (OperationWrapper op : _monitors) {
                op.stop();
                op.waitForCompletion();
            }

            setRunning(false);
            _mainThreadPump.post(() -> onFinished(_canceled));

        }).start();
    }

    protected int getContentViewResource() {
        return 0;
    }

    protected void onFinished(boolean canceled) {
        setResult(canceled
                ? MainActivity.RESULT_CODE_STRESS_TEST_CANCELED
                : MainActivity.RESULT_CODE_STRESS_TEST_FINISHED_SUCCESSFULLY);
        NativeInvoker.shutdownSuite();
        finish();
    }

    /**
     * Wraps java/native operations with a common interface for managing lifecycle
     */
    private static class OperationWrapper {
        private Configuration.Operation _operation;
        private BaseHostActivity _activity;
        private BaseOperation.Mode _mode;
        private BaseOperation _javaOperation;
        private NativeOperationHandle _nativeOperation;

        OperationWrapper(String suiteId, String description,
                         Configuration.Operation operation,
                         BaseHostActivity activity,
                         BaseOperation.Mode mode) {

            this._operation = operation;
            this._activity = activity;
            this._mode = mode;

            // attempt to load test & stressors (they might be java-based
            // after all), punting to native
            _javaOperation = Factory.create(suiteId,
                    operation.getId(),
                    operation.getConfigurationJson(),
                    activity,
                    mode);

            if (_javaOperation == null) {
                _nativeOperation = loadNativeOperation(suiteId, description,
                        operation);
                if (_nativeOperation == null) {
                    throw new IllegalStateException(
                            "Unable to create java or native operation of type: \""
                                    + operation.getId() + "\"");
                } else {
                    Log.d(TAG, "Loaded Native Operation " + operation.getId()
                            + " with handle: " + nativeHandle());
                }
            } else {
                Log.d(TAG, "Loaded Java Operation " + operation.getId()
                        + " from: " + _javaOperation.getClass().getCanonicalName());
            }
        }

        int nativeHandle() {
            if (_nativeOperation != null) {
                return _nativeOperation.nativeHandle;
            }
            return -1;
        }

        String getDescription() {
            if (_javaOperation != null) {
                return String.format(Locale.getDefault(),
                        "<Java Operation (%s) id: %s>",
                        _javaOperation.getClass().getCanonicalName(),
                        _operation.getId());
            }
            return String.format(Locale.getDefault(),
                    "<Native Operation (%d) id: %s>",
                    nativeHandle(),
                    _operation.getId());
        }

        private NativeOperationHandle
        loadNativeOperation(String suiteId, String description,
                            Configuration.Operation operation) {
            NativeOperationHandle h = new NativeOperationHandle();
            h.nativeHandle = NativeInvoker.createOperation(suiteId, description,
                    operation.getId(),
                    _mode.getValue());

            if (h.nativeHandle >= 0) {
                h.id = operation.getId();
                h.jsonConfiguration = operation.getConfigurationJson();
                return h;
            }
            return null;
        }

        void start(long durationMillis) {
            Log.d(TAG, "starting " + getDescription());
            if (_javaOperation != null) {
                _javaOperation.start(durationMillis);
            } else {
                NativeInvoker.startOperation(_nativeOperation.nativeHandle,
                        durationMillis, _nativeOperation.jsonConfiguration);
            }
        }

        void stop() {
            Log.d(TAG, "canceling " + getDescription());
            if (_javaOperation != null) {
                _javaOperation.stop();
            } else {
                NativeInvoker.stopOperation(_nativeOperation.nativeHandle);
            }
        }

        void waitForCompletion() {
            Log.d(TAG, "Waiting on " + getDescription());
            if (_javaOperation != null) {
                _javaOperation.waitForCompletion();
            } else {
                NativeInvoker.waitForOperation(_nativeOperation.nativeHandle);
            }
        }
    }

    private void setRunning(boolean running) {
        _running = running;
        ((ACTApplication) getApplication()).setIsRunningTest(running);
    }

    private static class NativeOperationHandle {
        String id;
        int nativeHandle;
        String jsonConfiguration;
        String jsonReport;
    }

}
