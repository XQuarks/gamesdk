import {buildDygraph} from './buildDygraph.js';
import {getDataExplorer} from './dataExplorer.js';
import {rowMetrics, rowTime} from './resultUtils.js';

document.body.addEventListener('click', evt => {
  if (evt.target.href) {
    evt.target.href =
        evt.target.href.replace('gs://', 'https://storage.cloud.google.com/');
  }
}, false);

/**
 *
 * @type {ResultsRow[]}
 */
const result = data;
const first = result[0];

result.sort((a, b) => rowTime(a) > rowTime(b) ? 1 : -1);

const section = document.getElementsByTagName('main')[0];

if ('extra' in first) {
  const extra = first.extra;
  if ('fromLauncher' in extra) {
    const fromLauncher = extra.fromLauncher;
    const h1 = document.createElement('h1');
    section.appendChild(h1);
    let fullName;
    if (fromLauncher.name.indexOf(fromLauncher.manufacturer) === -1) {
      fullName = `${fromLauncher.manufacturer} ${fromLauncher.name}`;
    } else {
      fullName = fromLauncher.name;
    }
    fullName += ` (${fromLauncher.id})`;
    h1.appendChild(document.createTextNode(fullName));
    const img = document.createElement('img');
    img.src = fromLauncher.thumbnailUrl;
    h1.appendChild(img);
  }
  {
    const paragraph = document.createElement('p');
    section.appendChild(paragraph);
    {
      const span = document.createElement('span');
      span.classList.add('link');
      const anchor = document.createElement('a');
      anchor.href = extra.resultsPage;
      anchor.appendChild(document.createTextNode('Results page'));
      span.appendChild(anchor);
      paragraph.appendChild(span);
    }
    {
      const span = document.createElement('span');
      span.classList.add('link');
      const toolLogs = extra.step.testExecutionStep.toolExecution.toolLogs;
      for (let idx = 0; idx !== toolLogs.length; idx++) {
        const anchor = document.createElement('a');
        span.appendChild(anchor);
        anchor.href = toolLogs[idx].fileUri;
        anchor.appendChild(document.createTextNode('Logs'));
      }
      paragraph.appendChild(span);
    }
  }
}

let deviceInfo;
for (let idx = 0; idx !== result.length; idx++) {
  const row = result[idx];
  if ('deviceInfo' in row) {
    deviceInfo = row.deviceInfo;
    break;
  }
}

{
  const paragraph = document.createElement('p');
  section.appendChild(paragraph);
  paragraph.appendChild(getDataExplorer(first));
  paragraph.appendChild(getDataExplorer(deviceInfo));
  {
    const span = document.createElement('span');
    span.classList.add('link');
    const anchor = document.createElement('a');
    const label = document.createTextNode('Download JSON');
    anchor.appendChild(label);
    anchor.setAttribute('href', '_blank');
    anchor.addEventListener('click', ev => {
      anchor.setAttribute(
          'href',
          'data:application/json,' +
              encodeURIComponent(JSON.stringify(result, null, ' ')));
    })
    anchor.setAttribute('download', 'data');
    span.appendChild(anchor);
    paragraph.appendChild(span);
  }
  {
    const span = document.createElement('span');
    span.classList.add('link');
    const anchor = document.createElement('a');
    const label = document.createTextNode('View JSON');
    anchor.appendChild(label);
    anchor.setAttribute('href', '_blank');
    anchor.addEventListener('click', ev => {
      ev.preventDefault();
      const holder = document.createElement('p');
      holder.style.height = '600px';
      paragraph.appendChild(holder);
      const script = document.createElement('script');
      script.src = 'https://cdnjs.cloudflare.com/ajax/libs/ace/1.4.12/ace.js';
      script.addEventListener('load', ev1 => {
        const jsonResults = ace.edit(holder, {
          mode: 'ace/mode/json',
          theme: 'ace/theme/crimson_editor',
          fontSize: '14px',
          readOnly: true
        });
        jsonResults.setValue(JSON.stringify(result, null, '\t'), -1);
      });
      document.body.appendChild(script);
    });
    span.appendChild(anchor);
    paragraph.appendChild(span);
  }
}
const graphDiv = document.createElement('div');
section.appendChild(graphDiv);
graphDiv.classList.add('graph');
const extrasDiv = document.createElement('div');
section.appendChild(extrasDiv);
buildDygraph(graphDiv, extrasDiv, deviceInfo, result);

let totalDuration = 0;
let durationCount = 0;
for (const row of result) {
  let metrics = rowMetrics(row);
  if (!metrics) {
    continue;
  }

  const advice = row.advice;
  if (advice && 'meta' in advice) {
    totalDuration += advice.meta.duration;
    durationCount++;
  }
}

if (durationCount > 0) {
  const paragraph = document.createElement('p');
  section.appendChild(paragraph);
  paragraph.appendChild(document.createTextNode(
      `Average duration ${(totalDuration / durationCount).toFixed(2)}`));
}
