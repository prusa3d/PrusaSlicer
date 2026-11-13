// Shared script of connect_logged_out.html and connect_logged_out_dark.html.
(function () {
  var setText = connectPages.setText;

  window.connectLoggedOut = {
    init: function (data) {
      var t = data.texts || {};
      connectPages.setModeTexts(t);
      setText('title', t.title);
      setText('intro', t.intro);
      setText('login', t.login);
    }
  };

  connectPages.el('login').onclick = function () { connectPages.post({ action: 'LOG_IN_IN_BROWSER' }); };
})();
