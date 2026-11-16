// Shared script of connect_local_setup.html and connect_local_setup_dark.html.
(function () {
  var post = connectPages.post;
  var el = connectPages.el;
  var setText = connectPages.setText;

  var texts = {
    searching: 'Searching the network…',
    nothing: 'No server found yet. Still searching…',
    stopped: 'Search stopped.',
    stop: 'Stop',
    search: 'Search again',
    use: 'Use',
    version: 'version',
    selfSigned: 'self-signed',
    expired: 'expired',
    notYetValid: 'not yet valid'
  };
  var shownCertificate = null;
  // The search of the network runs only when the user asks for it.
  var started = false;
  var isSearching = false;
  var nothingFound = false;
  var items = [];

  var serverIcon =
    "<svg aria-hidden='true' fill='none' stroke='currentColor' stroke-linecap='round' stroke-linejoin='round' stroke-width='1.8' viewBox='0 0 24 24'>" +
    "<rect height='7' rx='2' width='18' x='3' y='3.5' /><rect height='7' rx='2' width='18' x='3' y='13.5' />" +
    "<path d='M7 7h.01M7 17h.01M11 7h2M11 17h2' /></svg>";

  function clearError() {
    el('error').textContent = '';
    el('url').classList.remove('invalid');
  }

  function hideCertificate() {
    shownCertificate = null;
    el('step-certificate').classList.add('hidden');
    el('cert-checking').classList.add('hidden');
    el('cert-error').textContent = '';
  }

  function useServer(url) {
    clearError();
    post({ action: 'SET_CONNECT_LOCAL_URL', url: url });
  }

  function render() {
    var list = el('list');
    while (list.firstChild) list.removeChild(list.firstChild);
    items.forEach(function (item) {
      var li = document.createElement('li');
      var glyph = document.createElement('div');
      glyph.className = 'glyph';
      glyph.innerHTML = serverIcon;
      var info = document.createElement('div');
      info.className = 'info';
      var name = document.createElement('div');
      name.className = 'server-name';
      name.textContent = item.name || item.hostname || item.address;
      var detail = document.createElement('div');
      detail.className = 'server-detail';
      var host = document.createElement('span');
      host.textContent = (item.hostname || item.address) + ':' + item.port;
      detail.appendChild(host);
      if (item.version) {
        var version = document.createElement('span');
        version.className = 'version';
        version.textContent = texts.version + ' ' + item.version;
        detail.appendChild(version);
      }
      info.appendChild(name);
      info.appendChild(detail);
      var use = document.createElement('button');
      use.type = 'button';
      use.className = 'btn btn-secondary btn-small';
      use.textContent = texts.use;
      use.onclick = function () {
        el('url').value = item.url;
        hideCertificate();
        useServer(item.url);
      };
      li.appendChild(glyph);
      li.appendChild(info);
      li.appendChild(use);
      list.appendChild(li);
    });
  }

  function renderState() {
    var active = started || isSearching || items.length > 0;
    el('start').classList.toggle('hidden', active);
    el('toggle').classList.toggle('hidden', !active);
    el('discovery-status').classList.toggle('hidden', !active);
    el('spinner').classList.toggle('hidden', !isSearching);
    el('toggle').textContent = isSearching ? texts.stop : texts.search;
    if (isSearching) {
      el('status').textContent = nothingFound && items.length === 0 ? texts.nothing : texts.searching;
    } else {
      el('status').textContent = texts.stopped;
    }
  }

  window.connectLocalSetup = {
    init: function (data) {
      var t = data.texts || {};
      Object.keys(texts).forEach(function (key) {
        if (typeof t[key] === 'string' && t[key].length > 0) texts[key] = t[key];
      });
      connectPages.setModeTexts(t, data.showSwitch);
      setText('title', t.title);
      setText('intro', t.intro);
      setText('url-label', t.address);
      setText('connect', t.connect);
      setText('cancel', t.cancel);
      setText('cert-back', t.cancel);
      setText('discovery-title', t.discoveryTitle);
      setText('discovery-intro', t.discoveryIntro);
      setText('start-text', t.discoveryStart);
      setText('checking-text', t.checking);
      setText('cert-checking-text', t.checking);
      setText('cert-title', t.certTitle);
      setText('cert-intro', t.certIntro);
      setText('cert-fingerprint-label', t.fingerprint);
      setText('cert-subject-label', t.subject);
      setText('cert-names-label', t.names);
      setText('cert-issuer-label', t.issuer);
      setText('cert-validity-label', t.validity);
      setText('cert-trust', t.trust);
      setText('cert-load', t.load);
      if (typeof t.placeholder === 'string') el('url').placeholder = t.placeholder;
      if (typeof data.url === 'string' && el('url').value === '') el('url').value = data.url;
      el('cancel-row').classList.toggle('hidden', !data.canCancel);
      render();
      renderState();
    },
    showError: function (message) {
      el('checking').classList.add('hidden');
      el('cert-checking').classList.add('hidden');
      if (shownCertificate !== null) {
        el('cert-error').textContent = message;
        return;
      }
      el('error').textContent = message;
      el('url').classList.add('invalid');
      el('url').focus();
    },
    checking: function (active) {
      el('checking').classList.toggle('hidden', !active || shownCertificate !== null);
      el('cert-checking').classList.toggle('hidden', !active || shownCertificate === null);
      el('connect').disabled = !!active;
      el('cert-trust').disabled = !!active;
    },
    showCertificate: function (data) {
      if (!data || typeof data.url !== 'string') return;
      shownCertificate = data;
      clearError();
      el('url').value = data.url;
      el('checking').classList.add('hidden');
      el('connect').disabled = false;
      el('cert-server').textContent = data.url;
      el('cert-error').textContent = data.error || '';
      el('cert-fingerprint').textContent = data.fingerprint || '';
      el('cert-subject').textContent = data.subject || '';
      el('cert-names').textContent = Array.isArray(data.names) && data.names.length > 0 ? data.names.join(', ') : '–';
      el('cert-issuer').textContent = (data.issuer || '') + (data.selfSigned ? ' (' + texts.selfSigned + ')' : '');
      var validity = el('cert-validity');
      validity.textContent = (data.notBefore || '') + ' – ' + (data.notAfter || '');
      var problem = data.expired ? texts.expired : (data.notYetValid ? texts.notYetValid : '');
      if (problem !== '') validity.textContent += ' (' + problem + ')';
      validity.classList.toggle('warning', problem !== '');
      el('cert-checking').classList.add('hidden');
      el('cert-trust').disabled = false;
      el('step-certificate').classList.remove('hidden');
      el('step-certificate').scrollIntoView({ block: 'nearest' });
    },
    found: function (list) {
      items = Array.isArray(list) ? list : [];
      render();
      renderState();
    },
    // started: a search ran since the page was loaded (older callers leave it out).
    searching: function (searching, nothing, wasStarted) {
      isSearching = !!searching;
      nothingFound = !!nothing;
      started = wasStarted === undefined ? started || isSearching : !!wasStarted;
      renderState();
    }
  };

  el('form').onsubmit = function (event) {
    event.preventDefault();
    hideCertificate();
    useServer(el('url').value);
    return false;
  };
  el('url').oninput = function () {
    clearError();
    // The certificate belongs to the address it was shown for.
    if (shownCertificate !== null && el('url').value !== shownCertificate.url) hideCertificate();
  };
  el('cert-trust').onclick = function () {
    if (shownCertificate === null) return;
    el('cert-error').textContent = '';
    post({ action: 'TRUST_CONNECT_LOCAL_CERTIFICATE', url: shownCertificate.url, fingerprint: shownCertificate.fingerprint });
  };
  el('cert-load').onclick = function () {
    if (shownCertificate === null) return;
    el('cert-error').textContent = '';
    post({ action: 'LOAD_CONNECT_LOCAL_CERTIFICATE', url: shownCertificate.url });
  };
  el('cert-back').onclick = function () {
    hideCertificate();
    el('checking').classList.add('hidden');
    el('connect').disabled = false;
    el('url').focus();
  };
  el('start').onclick = function () {
    started = true;
    isSearching = true;
    renderState();
    post({ action: 'CONNECT_LOCAL_DISCOVERY', enabled: true });
  };
  el('toggle').onclick = function () {
    post({ action: 'CONNECT_LOCAL_DISCOVERY', enabled: !isSearching });
  };
  el('cancel').onclick = function () {
    post({ action: 'CANCEL_CONNECT_LOCAL_SETUP' });
  };
  renderState();
})();
