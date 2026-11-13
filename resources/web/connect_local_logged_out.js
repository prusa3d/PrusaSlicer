// Shared script of connect_local_logged_out.html and connect_local_logged_out_dark.html.
(function () {
  var post = connectPages.post;
  var el = connectPages.el;
  var setText = connectPages.setText;

  window.connectLocalLoggedOut = {
    init: function (data) {
      var t = data.texts || {};
      connectPages.setModeTexts(t);
      setText('title', t.title);
      setText('intro', t.intro);
      setText('login', t.login);
      setText('change', t.change);
      setText('server', data.server);
      setText('forget', t.forget);
      var trusted = typeof data.trust === 'string' && data.trust.length > 0;
      el('trust').textContent = trusted ? data.trust : '';
      el('trust-row').classList.toggle('hidden', !trusted);
    }
  };

  el('login').onclick = function () { post({ action: 'LOG_IN_IN_BROWSER' }); };
  el('change').onclick = function () { post({ action: 'SHOW_CONNECT_LOCAL_SETUP' }); };
  el('forget').onclick = function () { post({ action: 'FORGET_CONNECT_LOCAL_CERTIFICATE' }); };
})();
