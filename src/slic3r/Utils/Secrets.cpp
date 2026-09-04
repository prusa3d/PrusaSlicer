#include "Secrets.hpp"

#include <cstdio>
#if wxUSE_SECRETSTORE 
#include <wx/secretstore.h>
#endif
namespace Slic3r {

#if wxUSE_SECRETSTORE 

static bool PrintResult(bool ok)
{
    wxPuts(ok ? "ok" : "ERROR");
    return ok;
}

bool SelfTest(wxSecretStore& store, const wxString& service)
{
    wxPrintf("Running the tests...\n");

    const wxString userTest("test");
    const wxSecretValue secret1(6, "secret");

    wxPrintf("Storing the password:\t");
    bool ok = store.Save(service, userTest, secret1);
    if ( !PrintResult(ok) )
    {
        // The rest of the tests will probably fail too, no need to continue.
        wxPrintf("Bailing out.\n");
        return false;
    }

    wxPrintf("Loading the password:\t");
    wxSecretValue secret;
    wxString user;
    ok = PrintResult(store.Load(service, user, secret) &&
                     user == userTest &&
                     secret == secret1);

    // Overwriting the password should work.
    const wxSecretValue secret2(6, "privet");

    wxPrintf("Changing the password:\t");
    if ( PrintResult(store.Save(service, user, secret2)) )
    {
        wxPrintf("Reloading the password:\t");
        if ( !PrintResult(store.Load(service, user, secret) &&
                         secret == secret2) )
            ok = false;
    }
    else
        ok = false;

    wxPrintf("Deleting the password:\t");
    if ( !PrintResult(store.Delete(service)) )
        ok = false;

    // This is supposed to fail now.
    wxPrintf("Deleting it again:\t");
    if ( !PrintResult(!store.Delete(service)) )
        ok = false;

    // And loading should fail too.
    wxPrintf("Loading after deleting:\t");
    if ( !PrintResult(!store.Load(service, user, secret)) )
        ok = false;

    if ( ok )
        wxPrintf("All tests passed!\n");

    return ok;
}

#endif //wxUSE_SECRETSTORE

bool check_secrets()
{
#if wxUSE_SECRETSTORE 
    wxSecretStore store = wxSecretStore::GetDefault();

    return SelfTest(store, "prusaslicer");
#else
    printf("wxSecret not supported.\n");
    return true;
#endif
}

} // namespace Slic3r
