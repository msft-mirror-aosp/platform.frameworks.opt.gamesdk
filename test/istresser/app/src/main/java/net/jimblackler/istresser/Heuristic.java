package net.jimblackler.istresser;

import static net.jimblackler.istresser.MainActivity.tryAlloc;
import static net.jimblackler.istresser.Utils.optLong;

import android.os.Build;
import android.os.Debug;
import org.json.JSONArray;
import org.json.JSONException;
import org.json.JSONObject;

/**
 * Wrapper class for methods related to memory management heuristics.
 */
class Heuristic {
  /**
   * The value a heuristic returns when asked for memory pressure on the device through the
   * getSignal method. GREEN indicates it is safe to allocate further, YELLOW indicates further
   * allocation shouldn't happen, and RED indicates high memory pressure.
   */
  static JSONObject checkHeuristics(JSONObject metrics, JSONObject baseline, JSONObject params,
                                    JSONObject deviceSettings) throws JSONException {
    JSONObject results = new JSONObject();
    if (!params.has("heuristics")) {
      return results;
    }
    JSONObject heuristics = params.getJSONObject("heuristics");

    int oomScore = metrics.getInt("oom_score");
    JSONObject constant = baseline.getJSONObject("constant");
    Long commitLimit = optLong(constant, "CommitLimit");
    Long vmSize = optLong(metrics, "VmSize");
    Long cached = optLong(metrics, "Cached");
    Long memAvailable = optLong(metrics, "MemAvailable");
    long availMem = metrics.getLong("availMem");

    JSONArray warnings = new JSONArray();

    if (heuristics.has("vmsize")) {
      if (commitLimit != null && vmSize != null &&
          vmSize > commitLimit * heuristics.getDouble("vmsize")) {
        JSONObject warning = new JSONObject();
        warning.put("vmsize", heuristics.get("vmsize"));
        warning.put("level", "red");
        warnings.put(warning);
      }
    }

    if (heuristics.has("oom")) {
      if (oomScore > heuristics.getLong("oom")) {
        JSONObject warning = new JSONObject();
        warning.put("oom", heuristics.get("oom"));
        warning.put("level", "red");
        warnings.put(warning);
      }
    }

    if (heuristics.has("try")) {
      if (!tryAlloc((int) Utils.getMemoryQuantity(heuristics.get("try")))) {
        JSONObject warning = new JSONObject();
        warning.put("try", heuristics.get("try"));
        warning.put("level", "red");
        warnings.put(warning);
      }
    }

    if (heuristics.has("low")) {
      if (metrics.optBoolean("lowMemory")) {
        JSONObject warning = new JSONObject();
        warning.put("low", heuristics.get("low"));
        warning.put("level", "red");
        warnings.put(warning);
      }
    }

    if (heuristics.has("cl")) {
      if (commitLimit != null && Debug.getNativeHeapAllocatedSize() >
          commitLimit * heuristics.getDouble("cl")) {
        JSONObject warning = new JSONObject();
        warning.put("cl", heuristics.get("cl"));
        warning.put("level", "red");
        warnings.put(warning);
      }
    }

    if (heuristics.has("avail")) {
      if (availMem < Utils.getMemoryQuantity(heuristics.get("avail"))) {
        JSONObject warning = new JSONObject();
        warning.put("avail", heuristics.get("avail"));
        warning.put("level", "red");
        warnings.put(warning);
      }
    }

    if (heuristics.has("cached")) {
      if (cached != null && cached != 0 &&
          cached * heuristics.getDouble("cached") < constant.getLong("threshold") / 1024) {
        JSONObject warning = new JSONObject();
        warning.put("cached", heuristics.get("cached"));
        warning.put("level", "red");
        warnings.put(warning);
      }
    }

    if (heuristics.has("avail2")) {
      if (memAvailable != null &&
          memAvailable < Utils.getMemoryQuantity(heuristics.get("avail2"))) {
        JSONObject warning = new JSONObject();
        warning.put("avail2", heuristics.get("avail2"));
        warning.put("level", "red");
        warnings.put(warning);
      }
    }

    JSONObject nativeAllocatedParams = heuristics.optJSONObject("nativeAllocated");
    long nativeAllocatedLimit = deviceSettings.optLong("nativeAllocated");
    if (nativeAllocatedParams != null && nativeAllocatedLimit > 0) {
      String level = null;
      if (Debug.getNativeHeapAllocatedSize()
          > nativeAllocatedLimit * nativeAllocatedParams.getDouble("red")) {
        level = "red";
      } else if (Debug.getNativeHeapAllocatedSize()
          > nativeAllocatedLimit * nativeAllocatedParams.getDouble("yellow")) {
        level = "yellow";
      }
      if (level != null) {
        JSONObject warning = new JSONObject();
        warning.put("nativeAllocated", heuristics.get("nativeAllocated"));
        warning.put("level", level);
        warnings.put(warning);
      }
    }

    JSONObject vmSizeParams = heuristics.optJSONObject("VmSize");
    long vmSizeLimit = deviceSettings.optLong("VmSize");
    if (vmSizeParams != null && vmSize != null && vmSizeLimit > 0) {
      String level = null;
      if (vmSize > vmSizeLimit * vmSizeParams.getDouble("red")) {
        level = "red";
      } else if (vmSize > vmSizeLimit * vmSizeParams.getDouble("yellow")) {
        level = "yellow";
      }
      if (level != null) {
        JSONObject warning = new JSONObject();
        warning.put("VmSize", heuristics.get("VmSize"));
        warning.put("level", level);
        warnings.put(warning);
      }
    }

    JSONObject oomScoreParams = heuristics.optJSONObject("oom_score");
    long oomScoreLimit = deviceSettings.optLong("oom_score");
    if (oomScoreParams != null && oomScoreLimit > 0) {
      String level = null;
      if (oomScore > oomScoreLimit * oomScoreParams.getDouble("red")) {
        level = "red";
      } else if (oomScore > oomScoreLimit * oomScoreParams.getDouble("yellow")) {
        level = "yellow";
      }
      if (level != null) {
        JSONObject warning = new JSONObject();
        warning.put("oom_score", heuristics.get("oom_score"));
        warning.put("level", level);
        warnings.put(warning);
      }
    }

    if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.M) {
      JSONObject summaryGraphicsParams = heuristics.optJSONObject("summary.graphics");
      long summaryGraphicsLimit = deviceSettings.optLong("summary.graphics");
      if (summaryGraphicsParams != null && summaryGraphicsLimit > 0) {
        String level = null;
        long summaryGraphics = metrics.getLong("summary.graphics");
        if (summaryGraphics > summaryGraphicsLimit * summaryGraphicsParams.getDouble("red")) {
          level = "red";
        } else if (summaryGraphics
            > summaryGraphicsLimit * summaryGraphicsParams.getDouble("yellow")) {
          level = "yellow";
        }
        if (level != null) {
          JSONObject warning = new JSONObject();
          warning.put("summary.graphics", heuristics.get("summary.graphics"));
          warning.put("level", level);
          warnings.put(warning);
        }
      }

      JSONObject summaryTotalPssParams = heuristics.optJSONObject("summary.total-pss");
      long summaryTotalPssLimit = deviceSettings.optLong("summary.total-pss");
      if (summaryTotalPssParams != null && summaryTotalPssLimit > 0) {
        String level = null;
        long summaryTotalPss = metrics.getLong("summary.total-pss");
        if (summaryTotalPss > summaryTotalPssLimit * summaryTotalPssParams.getDouble("red")) {
          level = "red";
        } else if (summaryTotalPss
            > summaryTotalPssLimit * summaryTotalPssParams.getDouble("yellow")) {
          level = "yellow";
        }
        if (level != null) {
          JSONObject warning = new JSONObject();
          warning.put("summary.total-pss", heuristics.get("summary.total-pss"));
          warning.put("level", level);
          warnings.put(warning);
        }
      }
    }

    JSONObject availMemParams = heuristics.optJSONObject("availMem");
    long availMemLimit = deviceSettings.optLong("availMem");
    if (availMemParams != null && availMemLimit > 0) {
      String level = null;
      if (availMem * availMemParams.getDouble("red") < availMemLimit) {
        level = "red";
      } else if (availMem * availMemParams.getDouble("yellow") < availMemLimit) {
        level = "yellow";
      }
      if (level != null) {
        JSONObject warning = new JSONObject();
        warning.put("availMem", heuristics.get("availMem"));
        warning.put("level", level);
        warnings.put(warning);
      }
    }

    JSONObject cachedParams = heuristics.optJSONObject("Cached");
    long cachedLimit = deviceSettings.optLong("Cached");
    if (cachedParams != null && cached != null && cachedLimit > 0) {
      String level = null;
      if (cached * cachedParams.getDouble("red") < cachedLimit) {
        level = "red";
      } else if (cached * cachedParams.getDouble("yellow") < cachedLimit) {
        level = "yellow";
      }
      if (level != null) {
        JSONObject warning = new JSONObject();
        warning.put("Cached", heuristics.get("Cached"));
        warning.put("level", level);
        warnings.put(warning);
      }
    }

    JSONObject memAvailableParams = heuristics.optJSONObject("MemAvailable");
    long memAvailableLimit = deviceSettings.optLong("MemAvailable");
    if (memAvailableParams != null && memAvailable != null && memAvailableLimit > 0) {
      String level = null;
      if (memAvailable * memAvailableParams.getDouble("red") < memAvailableLimit) {
        level = "red";
      } else if (memAvailable * memAvailableParams.getDouble("yellow") < memAvailableLimit) {
        level = "yellow";
      }
      if (level != null) {
        JSONObject warning = new JSONObject();
        warning.put("MemAvailable", heuristics.get("MemAvailable"));
        warning.put("level", level);
        warnings.put(warning);
      }
    }
    results.put("warnings", warnings);

    return results;
  }
}
