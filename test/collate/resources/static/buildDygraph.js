import {getValues, rowMetrics} from "./resultUtils.js";

const onTrimCodes = {
  0: 'TRIM_MEMORY_UNKNOWN',
  5: 'TRIM_MEMORY_RUNNING_MODERATE',
  10: 'TRIM_MEMORY_RUNNING_LOW',
  15: 'TRIM_MEMORY_RUNNING_CRITICAL',
  20: 'TRIM_MEMORY_UI_HIDDEN',
  40: 'TRIM_MEMORY_BACKGROUND',
  60: 'TRIM_MEMORY_MODERATE',
  80: 'TRIM_MEMORY_COMPLETE'
};

function hashCode(str) {
  let hash = 0;
  for (let i = 0; i < str.length; i++) {
    hash = str.charCodeAt(i) + (hash << 5) - hash;
  }
  return hash;
}

export function buildDygraph(graphDiv, extrasDiv, deviceInfo, result) {
  const highlights = [];

  const annotations = [];
  let pausedStart = -1;
  let lowMemoryStart = -1;
  let serviceCrashedStart = -1;
  let allocFailedStart = -1;
  let activityPausedStart = -1;
  const graphData = [];

  const fields = ['Time'];
  let time = 0;
  const series = {};
  const combined = {};
  const rowOut = [0];
  for (const row of result) {
    let metrics = rowMetrics(row);
    if (!metrics) {
      continue;
    }
    time = (metrics.meta.time - deviceInfo.baseline.meta.time) / 1000;

    if ('testMetrics' in row) {
      let applicationAllocated = 0;
      for (const value of Object.values(row.testMetrics)) {
        applicationAllocated += value;
      }
      combined.applicationAllocated = applicationAllocated;
    }

    rowOut[0] = time;

    getValues(combined, metrics, '');
    delete combined.time;
    delete combined.onTrim;
    const advice = row.advice;

    if (advice && 'predictions' in advice && 'applicationAllocated' in combined) {
      const predictions = advice.predictions;
      for (const [field, value] of Object.entries(predictions).sort()) {
        const totalPrediction = value + combined.applicationAllocated;
        combined['prediction_' + field] =
            totalPrediction >= 0 ? totalPrediction : 0;
      }
    }

    for (const [field, value] of Object.entries(combined)) {
      if (typeof value !== 'number') {
        continue;
      }
      if (fields.indexOf(field) === -1) {
        series[field] = {color: `hsl(${hashCode(field) % 360}, 100%, 30%)`};
        if (deviceInfo.params.heuristics &&
            field in deviceInfo.params.heuristics) {
          series[field].strokeWidth = 3;
        }
        fields.push(field);
        for (const existingRow of graphData) {
          existingRow.push(0);
        }
      }
      rowOut[fields.indexOf(field)] =
          field === 'proc/oom_score' ? value : value / (1024 * 1024);
    }

    graphData.push(rowOut.slice(0));

    if ('exiting' in row) {
      annotations.push({
        series: 'applicationAllocated',
        x: time,
        shortText: 'E',
        text: 'Exiting'
      });
    }
    if ('onDestroy' in row) {
      annotations.push({
        series: 'applicationAllocated',
        x: time,
        shortText: 'D',
        text: 'onDestroy'
      });
    }
    if ('onTrim' in metrics) {
      annotations.push({
        series: 'applicationAllocated',
        x: time,
        shortText: metrics.onTrim,
        text: onTrimCodes[metrics.onTrim]
      });
    }

    if ('mapTester' in metrics) {
      annotations.push(
          {series: 'nativeAllocated', x: time, shortText: 'M', text: 'M'});
    }
    if (advice && 'warnings' in advice) {
      const warnings = advice.warnings;
      for (let idx = 0; idx !== warnings.length; idx++) {
        let warning = warnings[idx];
        if (warning.level !== 'red') {
          continue;
        }
        for (const [key, value] of Object.entries(warning)) {
          if (key === 'level') {
            continue;
          }
          annotations.push({
            series: 'applicationAllocated',
            x: time,
            shortText: key,
            text: key
          });
        }
      }
    }

    if ('criticalLogLines' in row) {
      annotations.push({
        series: 'applicationAllocated',
        x: time,
        shortText: 'L',
        text: row.criticalLogLines[0]
      });
    }

    if ('activityPaused' in row) {
      if (row['activityPaused']) {
        if (activityPausedStart === -1) {
          activityPausedStart = time;
        }
      } else if (activityPausedStart !== -1) {
        highlights.push([activityPausedStart, time, 'lightgrey']);
        activityPausedStart = -1;
      }
    }

    let paused = !!row['paused'];
    if (paused && pausedStart === -1) {
      pausedStart = time;
    } else if (!row.paused && pausedStart !== -1) {
      highlights.push([pausedStart, time, 'yellow']);
      pausedStart = -1;
    }

    let lowMemory = !!(metrics.MemoryInfo && metrics.MemoryInfo.lowMemory);
    if (lowMemory && lowMemoryStart === -1) {
      lowMemoryStart = time;
    } else if (!lowMemory && lowMemoryStart !== -1) {
      highlights.push([lowMemoryStart, time, 'pink']);
      lowMemoryStart = -1;
    }

    let serviceCrashed = !!row['serviceCrashed'];
    if (serviceCrashed && serviceCrashedStart === -1) {
      serviceCrashedStart = time;
    } else if (!serviceCrashed && serviceCrashedStart !== -1) {
      highlights.push([serviceCrashedStart, time, 'cyan']);
      serviceCrashedStart = -1;
    }

    let allocFailed = !!row['allocFailed'] || !!row['mmapAnonFailed'] ||
        !!row['mmapFileFailed'];
    if (allocFailed && allocFailedStart === -1) {
      allocFailedStart = time;
    } else if (!allocFailed && allocFailedStart !== -1) {
      highlights.push([allocFailedStart, time, 'lightblue']);
      allocFailedStart = -1;
    }
  }

  if (activityPausedStart !== -1) {
    highlights.push([activityPausedStart, time, 'lightgrey']);
    allocFailedStart = -1;
  }
  if (pausedStart !== -1) {
    highlights.push([pausedStart, time, 'yellow']);
  }
  if (lowMemoryStart !== -1) {
    highlights.push([lowMemoryStart, time, 'pink']);
  }
  if (serviceCrashedStart !== -1) {
    highlights.push([serviceCrashedStart, time, 'cyan']);
  }
  if (allocFailedStart !== -1) {
    highlights.push([allocFailedStart, time, 'lightblue']);
  }

  if ('applicationAllocated' in series) {
    series.applicationAllocated.strokeWidth = 4;
    series.applicationAllocated.color = 'black';
  }
  if ('proc/oom_score' in series) {
    series['proc/oom_score'].axis = 'y2';
  }
  let wasHighlighted = null;
  const graph = new Dygraph(graphDiv, graphData, {
    height: 725,
    labels: fields,
    showLabelsOnHighlight: false,
    highlightCircleSize: 4,
    strokeWidth: 1,
    strokeBorderWidth: 1,
    highlightSeriesOpts:
        {strokeWidth: 2, strokeBorderWidth: 1, highlightCircleSize: 5},
    series: series,
    axes: {
      x: {drawGrid: false},
      y: {independentTicks: true},
      y2: {valueRange: [0, 1000], independentTicks: true}
    },
    ylabel: 'MB',
    y2label: 'OOM Score',

    underlayCallback: (canvas, area, g) => {
      for (const [start, end, fillStyle] of highlights) {
        canvas.fillStyle = fillStyle;
        canvas.fillRect(
            g.toDomCoords(start, 0)[0], area.y,
            g.toDomCoords(end, 0)[0] - g.toDomCoords(start, 0)[0], area.h);
      }
    },
    pointClickCallback: (e, p) => {
      alert(p.name + '\n\n' + p.yval);
    },
    highlightCallback: (e, lastx, selPoints, lastRow_, highlightSet) => {
      const elements = document.getElementsByName(highlightSet);
      if (elements.length !== 1) {
        return;
      }
      const input = elements[0];
      if (wasHighlighted !== null) {
        wasHighlighted.style.color = wasHighlighted.style.backgroundColor;
        wasHighlighted.style.backgroundColor = '';
        wasHighlighted = null;
      }
      input.parentElement.style.backgroundColor =
          input.parentElement.style.color;
      input.parentElement.style.color = 'white';
      wasHighlighted = input.parentElement;
    },
    unhighlightCallback: (e) => {
      if (wasHighlighted !== null) {
        wasHighlighted.style.color = wasHighlighted.style.backgroundColor;
        wasHighlighted.style.backgroundColor = '';
        wasHighlighted = null;
      }
    }

  });

  const fieldsets = {};
  const form = document.createElement('form');
  form.classList.add('seriesForm');
  extrasDiv.appendChild(form);

  for (let idx = 0; idx !== fields.length; idx++) {
    const field = fields[idx];
    if (!(field in series)) {
      continue;
    }
    const prefixPos = field.lastIndexOf('/');
    const prefix = prefixPos === -1 ? 'root' : field.substring(0, prefixPos);
    const suffix = prefixPos === -1 ? field : field.substring(prefixPos + 1);
    let fieldset;
    if (prefix in fieldsets) {
      fieldset = fieldsets[prefix];
    } else {
      fieldset = document.createElement('fieldset');
      const legend = document.createElement('legend');
      legend.appendChild(document.createTextNode(prefix));
      fieldset.appendChild(legend);
      fieldsets[prefix] = fieldset;
      form.appendChild(fieldset);
    }

    const _series = series[field];
    const label = document.createElement('label');
    const input = document.createElement('input');
    label.appendChild(input);
    label.style.setProperty('color', _series.color);
    label.addEventListener('mouseover', evt => {
      graph.setSelection(0, field);
      label.style.backgroundColor = label.style.color;
      label.style.color = 'white';
      wasHighlighted = label;
    });
    label.addEventListener('mouseout', evt => {
      if (wasHighlighted !== null) {
        wasHighlighted.style.color = wasHighlighted.style.backgroundColor;
        wasHighlighted.style.backgroundColor = '';
        wasHighlighted = null;
      }
    });
    input.setAttribute('type', 'checkbox');
    input.setAttribute('name', field);
    if (localStorage.getItem(field) === 'false') {
      graph.setVisibility(idx - 1, false);
    } else {
      input.checked = true;
    }

    input.addEventListener('change', evt => {
      localStorage.setItem(field, input.checked);
      graph.setVisibility(idx - 1, input.checked);
    });
    label.appendChild(document.createTextNode(suffix));
    fieldset.appendChild(label);
  }
  graph.ready(() => graph.setAnnotations(annotations));
}