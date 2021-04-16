package net.jimblackler.istresser;

import static com.google.android.apps.internal.games.memoryadvice_common.ConfigUtils.getMemoryQuantity;
import static com.google.android.apps.internal.games.memoryadvice_common.ConfigUtils.getOrDefault;
import static net.jimblackler.istresser.Utils.getDuration;

import android.content.Context;
import android.util.Log;
import com.google.android.apps.internal.games.memoryadvice.MemoryAdvisor;
import com.google.android.apps.internal.games.memoryadvice.MemoryWatcher;
import java.io.IOException;
import java.util.LinkedHashMap;
import java.util.Map;
import java.util.Timer;
import java.util.TimerTask;

class MemoryTest implements MemoryWatcher.Client {
  private static final String TAG = MemoryTest.class.getSimpleName();

  private final boolean yellowLightTesting;
  private final long mallocBytesPerMillisecond;
  private final long glAllocBytesPerMillisecond;
  private final long vkAllocBytesPerMillisecond;
  private final long mmapFileBytesPerMillisecond;
  private final long mmapAnonBytesPerMillisecond;
  private final long mmapAnonBlock;
  private final long mmapFileBlock;
  private final long memoryToFreePerCycle;
  private final long delayBeforeRelease;
  private final long delayAfterRelease;
  private final ResultsReceiver resultsReceiver;
  private final MemoryAdvisor memoryAdvisor;
  private final Timer timer = new Timer();
  private final TestSurface testSurface;
  private final MmapFileGroup mmapFiles;
  private long allocationStartedTime = System.currentTimeMillis();
  private long nativeAllocatedByTest;
  private long vkAllocatedByTest;
  private long mmapAnonAllocatedByTest;
  private long mmapFileAllocatedByTest;

  MemoryTest(Context context, MemoryAdvisor memoryAdvisor, TestSurface testSurface,
      Map<String, Object> params, ResultsReceiver resultsReceiver) {
    this.testSurface = testSurface;
    this.resultsReceiver = resultsReceiver;
    this.memoryAdvisor = memoryAdvisor;

    yellowLightTesting = Boolean.TRUE.equals(params.get("yellowLightTesting"));
    mallocBytesPerMillisecond = getMemoryQuantity(getOrDefault(params, "malloc", 0));
    glAllocBytesPerMillisecond = getMemoryQuantity(getOrDefault(params, "glTest", 0));
    vkAllocBytesPerMillisecond = getMemoryQuantity(getOrDefault(params, "vkTest", 0));

    delayBeforeRelease = getDuration(getOrDefault(params, "delayBeforeRelease", "1s"));
    delayAfterRelease = getDuration(getOrDefault(params, "delayAfterRelease", "1s"));

    memoryToFreePerCycle = getMemoryQuantity(getOrDefault(params, "memoryToFreePerCycle", "500M"));

    Map<String, Object> mmapAnon = (Map<String, Object>) params.get("mmapAnon");
    if (mmapAnon == null) {
      mmapAnonBlock = 0;
      mmapAnonBytesPerMillisecond = 0;
    } else {
      mmapAnonBlock = getMemoryQuantity(getOrDefault(mmapAnon, "blockSize", "2M"));
      mmapAnonBytesPerMillisecond =
          getMemoryQuantity(getOrDefault(mmapAnon, "allocPerMillisecond", 0));
    }

    Map<String, Object> mmapFile = (Map<String, Object>) params.get("mmapFile");
    if (mmapFile == null) {
      mmapFiles = null;
      mmapFileBlock = 0;
      mmapFileBytesPerMillisecond = 0;
    } else {
      int mmapFileCount = ((Number) getOrDefault(mmapFile, "count", 10)).intValue();
      long mmapFileSize = getMemoryQuantity(getOrDefault(mmapFile, "fileSize", "4K"));
      String mmapPath = context.getCacheDir().toString();
      try {
        mmapFiles = new MmapFileGroup(mmapPath, mmapFileCount, mmapFileSize);
      } catch (IOException e) {
        throw new IllegalStateException(e);
      }
      mmapFileBlock = getMemoryQuantity(getOrDefault(mmapFile, "blockSize", "2M"));
      mmapFileBytesPerMillisecond =
          getMemoryQuantity(getOrDefault(mmapFile, "allocPerMillisecond", 0));
    }
  }

  @Override
  public void newState(MemoryAdvisor.MemoryState state) {
    Log.i(TAG, "New memory state: " + state.name());
  }

  @Override
  public void receiveAdvice(Map<String, Object> advice) {
    Map<String, Object> report = new LinkedHashMap<>();
    report.put("advice", advice);
    if (allocationStartedTime == -1) {
      report.put("paused", true);
    } else {
      long sinceAllocationStarted = System.currentTimeMillis() - allocationStartedTime;
      if (sinceAllocationStarted > 0) {
        boolean shouldAllocate = true;
        switch (MemoryAdvisor.getMemoryState(advice)) {
          case APPROACHING_LIMIT:
            if (yellowLightTesting) {
              shouldAllocate = false;
            }
            break;
          case CRITICAL:
            shouldAllocate = false;
            if (yellowLightTesting) {
              MainActivity.freeMemory(memoryToFreePerCycle);
            } else {
              // Allocating 0 MB
              releaseMemory();
            }
            break;
        }
        if (shouldAllocate) {
          if (mallocBytesPerMillisecond > 0) {
            long owed = sinceAllocationStarted * mallocBytesPerMillisecond - nativeAllocatedByTest;
            if (owed > 0) {
              boolean succeeded = MainActivity.nativeConsume(owed);
              if (succeeded) {
                nativeAllocatedByTest += owed;
              } else {
                report.put("allocFailed", true);
              }
            }
          }
          if (glAllocBytesPerMillisecond > 0) {
            long target = sinceAllocationStarted * glAllocBytesPerMillisecond;
            testSurface.getRenderer().setTarget(target);
          }

          if (vkAllocBytesPerMillisecond > 0) {
            long owed = sinceAllocationStarted * vkAllocBytesPerMillisecond - vkAllocatedByTest;
            if (owed > 0) {
              long allocated = MainActivity.vkAlloc(owed);
              if (allocated >= owed) {
                vkAllocatedByTest += owed;
              } else {
                report.put("allocFailed", true);
              }
            }
          }

          if (mmapAnonBytesPerMillisecond > 0) {
            long owed =
                sinceAllocationStarted * mmapAnonBytesPerMillisecond - mmapAnonAllocatedByTest;
            if (owed > mmapAnonBlock) {
              long allocated = MainActivity.mmapAnonConsume(owed);
              if (allocated == 0) {
                report.put("mmapAnonFailed", true);
              } else {
                mmapAnonAllocatedByTest += allocated;
              }
            }
          }
          if (mmapFileBytesPerMillisecond > 0) {
            long owed =
                sinceAllocationStarted * mmapFileBytesPerMillisecond - mmapFileAllocatedByTest;
            if (owed > mmapFileBlock) {
              MmapFileInfo file = mmapFiles.alloc(owed);
              long allocated = MainActivity.mmapFileConsume(
                  file.getPath(), file.getAllocSize(), file.getOffset());
              if (allocated == 0) {
                report.put("mmapFileFailed", true);
              } else {
                mmapFileAllocatedByTest += allocated;
              }
            }
          }
        }
      }
    }

    Map<String, Object> testMetrics = new LinkedHashMap<>();
    if (vkAllocatedByTest > 0) {
      testMetrics.put("vkAllocatedByTest", vkAllocatedByTest);
    }
    if (nativeAllocatedByTest > 0) {
      testMetrics.put("nativeAllocatedByTest", nativeAllocatedByTest);
    }
    if (mmapAnonAllocatedByTest > 0) {
      testMetrics.put("mmapAnonAllocatedByTest", mmapAnonAllocatedByTest);
    }
    if (mmapAnonAllocatedByTest > 0) {
      testMetrics.put("mmapFileAllocatedByTest", mmapFileAllocatedByTest);
    }

    TestRenderer renderer = testSurface.getRenderer();
    long glAllocated = renderer.getAllocated();
    if (glAllocated > 0) {
      testMetrics.put("gl_allocated", glAllocated);
    }
    if (renderer.getFailed()) {
      report.put("allocFailed", true);
    }
    report.put("testMetrics", testMetrics);

    resultsReceiver.accept(report);
  }

  private void runAfterDelay(Runnable runnable, long delay) {
    timer.schedule(new TimerTask() {
      @Override
      public void run() {
        runnable.run();
      }
    }, delay);
  }

  private void releaseMemory() {
    allocationStartedTime = -1;

    runAfterDelay(() -> {
      Map<String, Object> report2 = new LinkedHashMap<>();
      report2.put("paused", true);
      report2.put("metrics", memoryAdvisor.getMemoryMetrics());

      resultsReceiver.accept(report2);

      if (nativeAllocatedByTest > 0) {
        nativeAllocatedByTest = 0;
        MainActivity.freeAll();
      }
      MainActivity.mmapAnonFreeAll();
      mmapAnonAllocatedByTest = 0;

      if (glAllocBytesPerMillisecond > 0) {
        if (testSurface != null) {
          testSurface.queueEvent(() -> {
            TestRenderer renderer = testSurface.getRenderer();
            renderer.release();
          });
        }
      }
      if (vkAllocBytesPerMillisecond > 0) {
        vkAllocatedByTest = 0;
        MainActivity.vkRelease();
      }

      runAfterDelay(new Runnable() {
        @Override
        public void run() {
          Map<String, Object> report = new LinkedHashMap<>();
          Map<String, Object> advice = memoryAdvisor.getAdvice();
          report.put("advice", advice);
          if (MemoryAdvisor.anyWarnings(advice)) {
            report.put("failedToClear", true);
            report.put("paused", true);
            runAfterDelay(this, delayAfterRelease);
          } else {
            allocationStartedTime = System.currentTimeMillis();
          }
          resultsReceiver.accept(report);
        }
      }, delayAfterRelease);
    }, delayBeforeRelease);
  }

  interface ResultsReceiver {
    void accept(Map<String, Object> results);
  }
}