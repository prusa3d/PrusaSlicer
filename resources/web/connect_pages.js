// Shared script of the slicer's own Connect tab pages (connect_logged_out, connect_local_setup,
// connect_local_logged_out): messages to PrusaSlicer.
window.connectPages = (function () {
  function post(message) {
    if (window._prusaSlicer) {
      window._prusaSlicer.postMessage(JSON.stringify(message));
    }
  }
  function el(id) { return document.getElementById(id); }
  function setText(id, value) {
    var node = el(id);
    if (node && typeof value === 'string' && value.length > 0) node.textContent = value;
  }

  // Each button of the switch names its action; the current mode has aria-current='page'.
  // The mode change reloads the tab, until then the switch shows the chosen mode.
  var buttons = Array.prototype.slice.call(document.querySelectorAll('.modes button'));
  buttons.forEach(function (button) {
    button.onclick = function () {
      if (button.getAttribute('aria-current') === 'page') return;
      buttons.forEach(function (other) {
        if (other === button) other.setAttribute('aria-current', 'page');
        else other.removeAttribute('aria-current');
        other.disabled = true;
      });
      post({ action: button.getAttribute('data-action') });
    };
  });

  return {
    post: post,
    el: el,
    setText: setText,
    // Translated labels of the switch, sent by PrusaSlicer in the init() of each page. The switch
    // is shown only while both services are enabled.
    setModeTexts: function (t, showSwitch) {
      setText('mode-cloud', t.modeCloud);
      setText('mode-local', t.modeLocal);
      setText('mode-tag', t.modeTag);
      var modes = el('modes');
      if (modes) {
        modes.classList.toggle('hidden', showSwitch !== true);
        if (typeof t.modeSwitch === 'string' && t.modeSwitch.length > 0) {
          modes.setAttribute('aria-label', t.modeSwitch);
        }
      }
    }
  };
})();
