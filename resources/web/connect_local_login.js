// Shared script of connect_local_login.html and connect_local_login_dark.html.
(function () {
  function el(id) { return document.getElementById(id); }
  function setText(id, value) {
    if (typeof value === 'string' && value.length > 0) el(id).textContent = value;
  }
  function show(id) {
    ['opening', 'waiting', 'failed'].forEach(function (section) {
      el(section).classList.toggle('hidden', section !== id);
    });
  }
  function send(event) {
    if (window.PrusaSlicerWebviewMessage) {
      window.PrusaSlicerWebviewMessage.postMessage(JSON.stringify({ event: event }));
    }
  }
  window.connectLocalLogin = {
    init: function (texts) {
      texts = texts || {};
      setText('opening-text', texts.opening);
      setText('waiting-title', texts.waitingTitle);
      setText('waiting-hint', texts.waitingHint);
      setText('waiting-status', texts.waitingStatus);
      setText('open-in-browser', texts.openInBrowser);
      setText('failed-title', texts.failedTitle);
      setText('failed-hint', texts.hint);
      setText('retry', texts.retry);
      setText('cancel', texts.cancel);
      setText('close', texts.close);
    },
    showOpening: function (text) {
      setText('opening-text', text);
      show('opening');
    },
    showWaiting: function (code, note) {
      el('user-code').textContent = code || '';
      el('waiting-note').textContent = note || '';
      show('waiting');
    },
    showError: function (reason, canRetry) {
      el('failed-reason').textContent = reason || '';
      el('retry').classList.toggle('hidden', !canRetry);
      show('failed');
    }
  };
  el('open-in-browser').onclick = function () { send('openInBrowser'); };
  el('retry').onclick = function () { send('retry'); };
  el('close').onclick = function () { send('closeDialog'); };
  el('cancel').onclick = function () { send('closeDialog'); };
})();
