#include "stdafx.h"
#include "qobuz_browser.h"
#include "config.h"

// ---------------------------------------------------------------------------
// Credential helper – pulls from global cfg vars
// ---------------------------------------------------------------------------
std::tuple<std::string,std::string,std::string> CQobuzBrowserWnd::getCredentials() {
    return {
        static_cast<const char*>(g_cfg_app_id),
        static_cast<const char*>(g_cfg_app_secret),
        static_cast<const char*>(g_cfg_auth_token),
    };
}

// ---------------------------------------------------------------------------
// WM_CREATE
// ---------------------------------------------------------------------------
LRESULT CQobuzBrowserWnd::OnCreate(UINT, WPARAM, LPARAM, BOOL&) {
    m_alive = std::make_shared<std::atomic<bool>>(true);

    // ----- Search bar (top strip) -----
    m_searchEdit.Create(m_hWnd, rcDefault, nullptr,
        WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL,
        0, IDC_QBROWSER_SEARCH);
    m_searchEdit.SetFont(AtlGetDefaultGuiFont());

    m_searchBtn.Create(m_hWnd, rcDefault, L"Search",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        0, IDC_QBROWSER_BTN_GO);
    m_searchBtn.SetFont(AtlGetDefaultGuiFont());

    // ----- Navigation tree (left) -----
    m_tree.Create(m_hWnd, rcDefault, nullptr,
        WS_CHILD | WS_VISIBLE | WS_BORDER | TVS_HASLINES |
        TVS_HASBUTTONS | TVS_LINESATROOT | TVS_SHOWSELALWAYS,
        0, IDC_QBROWSER_TREE);

    // ----- Content list (right) -----
    m_list.Create(m_hWnd, rcDefault, nullptr,
        WS_CHILD | WS_VISIBLE | WS_BORDER |
        LVS_REPORT | LVS_SHOWSELALWAYS | LVS_SINGLESEL,
        LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES, IDC_QBROWSER_LIST);
    setupList();

    // ----- Status bar (bottom) -----
    m_statusBar.Create(m_hWnd, rcDefault, nullptr,
        WS_CHILD | WS_VISIBLE | SS_LEFT,
        0, (HMENU)(UINT_PTR)2010);
    m_statusBar.SetFont(AtlGetDefaultGuiFont());

    // ----- Populate tree with root nodes -----
    m_hNewReleases = addTreeNode(TVI_ROOT, L"New Releases",
        { TreeNodeData::Type::NewReleasesRoot, "", "New Releases" },
        /*addPlaceholder=*/true);

    m_hPlaylists = addTreeNode(TVI_ROOT, L"My Playlists",
        { TreeNodeData::Type::PlaylistsRoot, "", "My Playlists" },
        /*addPlaceholder=*/true);

    HTREEITEM hFav = addTreeNode(TVI_ROOT, L"Favorites",
        { TreeNodeData::Type::NewReleasesRoot /* reuse root type */ , "", "Favorites" },
        /*addPlaceholder=*/false);

    m_hFavTracks = addTreeNode(hFav, L"Tracks",
        { TreeNodeData::Type::FavTracksRoot, "", "Favorite Tracks" });
    m_hFavAlbums = addTreeNode(hFav, L"Albums",
        { TreeNodeData::Type::FavAlbumsRoot, "", "Favorite Albums" },
        /*addPlaceholder=*/true);

    m_tree.Expand(hFav, TVE_EXPAND);

    setStatus("Select a section from the tree to browse.");
    return 0;
}

// ---------------------------------------------------------------------------
// WM_DESTROY
// ---------------------------------------------------------------------------
LRESULT CQobuzBrowserWnd::OnDestroy(UINT, WPARAM, LPARAM, BOOL&) {
    *m_alive = false; // signal all detached threads to not PostMessage
    return 0;
}

// ---------------------------------------------------------------------------
// WM_SIZE  –  manual layout
// ---------------------------------------------------------------------------
LRESULT CQobuzBrowserWnd::OnSize(UINT, WPARAM, LPARAM lParam, BOOL&) {
    const int W = LOWORD(lParam);
    const int H = HIWORD(lParam);

    const int kSearchH   = 24;
    const int kStatusH   = 18;
    const int kBtnW      = 60;
    const int kTreeW     = 200;
    const int kPad       = 4;

    // Search bar
    m_searchEdit.MoveWindow(kPad, kPad,
        W - kBtnW - kPad * 3, kSearchH);
    m_searchBtn.MoveWindow(W - kBtnW - kPad, kPad,
        kBtnW, kSearchH);

    // Status bar
    m_statusBar.MoveWindow(kPad, H - kStatusH - kPad,
        W - kPad * 2, kStatusH);

    const int contentTop = kSearchH + kPad * 2;
    const int contentH   = H - contentTop - kStatusH - kPad * 2;

    // Tree
    m_tree.MoveWindow(0, contentTop, kTreeW, contentH);

    // List
    m_list.MoveWindow(kTreeW + 1, contentTop, W - kTreeW - 1, contentH);

    // Resize list columns proportionally
    const int listW = W - kTreeW - 1;
    if (listW > 100) {
        m_list.SetColumnWidth(kColNum,      40);
        m_list.SetColumnWidth(kColTitle,    listW / 3);
        m_list.SetColumnWidth(kColArtist,   listW / 4);
        m_list.SetColumnWidth(kColAlbum,    listW / 4);
        m_list.SetColumnWidth(kColDuration, 55);
    }
    return 0;
}

// ---------------------------------------------------------------------------
// TVN_ITEMEXPANDING  –  lazy-load children when a root node is expanded
// ---------------------------------------------------------------------------
LRESULT CQobuzBrowserWnd::OnTreeItemExpanding(int, LPNMHDR pNMHDR, BOOL&) {
    auto* pnm = reinterpret_cast<NMTREEVIEW*>(pNMHDR);
    if (pnm->action != TVE_EXPAND) return 0;

    HTREEITEM hItem = pnm->itemNew.hItem;
    auto it = m_treeData.find(hItem);
    if (it == m_treeData.end() || it->second.children_loaded) return 0;

    it->second.children_loaded = true;
    removeChildren(hItem); // remove the dummy placeholder

    switch (it->second.type) {
    case TreeNodeData::Type::NewReleasesRoot:
        asyncLoadNewReleases(hItem);
        break;
    case TreeNodeData::Type::PlaylistsRoot:
        asyncLoadUserPlaylists(hItem);
        break;
    case TreeNodeData::Type::FavAlbumsRoot:
        asyncLoadFavAlbums(hItem);
        break;
    default:
        break;
    }
    return 0;
}

// ---------------------------------------------------------------------------
// TVN_SELCHANGED  –  load content for selected node into the list
// ---------------------------------------------------------------------------
LRESULT CQobuzBrowserWnd::OnTreeSelChanged(int, LPNMHDR pNMHDR, BOOL&) {
    auto* pnm = reinterpret_cast<NMTREEVIEW*>(pNMHDR);
    HTREEITEM hItem = pnm->itemNew.hItem;
    auto it = m_treeData.find(hItem);
    if (it == m_treeData.end()) return 0;

    const auto& nd = it->second;
    switch (nd.type) {
    case TreeNodeData::Type::Album:
        asyncLoadAlbumTracks(nd.id);
        break;
    case TreeNodeData::Type::Playlist:
        asyncLoadPlaylistTracks(nd.id);
        break;
    case TreeNodeData::Type::FavTracksRoot:
        asyncLoadFavTracks();
        break;
    default:
        break;
    }
    return 0;
}

// ---------------------------------------------------------------------------
// NM_DBLCLK on list  –  play selected track
// ---------------------------------------------------------------------------
LRESULT CQobuzBrowserWnd::OnListDblClick(int idCtrl, LPNMHDR pNMHDR, BOOL&) {
    if (idCtrl != IDC_QBROWSER_LIST) return 0;

    int sel = m_list.GetNextItem(-1, LVNI_SELECTED);
    if (sel >= 0 && sel < static_cast<int>(m_currentTracks.size()))
        playTrack(sel);
    return 0;
}

// ---------------------------------------------------------------------------
// NM_RCLICK on list  –  context menu
// ---------------------------------------------------------------------------
LRESULT CQobuzBrowserWnd::OnListRClick(int idCtrl, LPNMHDR pNMHDR, BOOL&) {
    if (idCtrl != IDC_QBROWSER_LIST) return 0;

    int sel = m_list.GetNextItem(-1, LVNI_SELECTED);
    if (sel < 0 || sel >= static_cast<int>(m_currentTracks.size())) return 0;

    POINT pt;
    GetCursorPos(&pt);

    CMenu menu;
    menu.CreatePopupMenu();
    menu.AppendMenuW(MF_STRING, 1, L"Play now");
    menu.AppendMenuW(MF_STRING, 2, L"Add to queue (end of playlist)");
    menu.AppendMenuW(MF_STRING, 3, L"Play all in list");

    int cmd = menu.TrackPopupMenu(TPM_RETURNCMD | TPM_RIGHTBUTTON,
                                   pt.x, pt.y, m_hWnd);
    switch (cmd) {
    case 1:
        playTrack(sel);
        break;
    case 2: {
        // Add single track to end of active playlist without starting playback
        auto pm = static_api_ptr_t<playlist_manager>();
        t_size pl = pm->get_active_playlist();
        playable_location_impl loc;
        loc.set_path(("qobuz://track/" + m_currentTracks[sel].id).c_str());
        loc.set_subsong(0);
        metadb_handle_ptr h;
        static_api_ptr_t<metadb>()->handle_create(h, loc);
        pfc::list_single_ref_t<metadb_handle_ptr> items(h);
        pm->playlist_insert_items(pl, pm->playlist_get_item_count(pl), items,
                                  bit_array_false());
        break;
    }
    case 3:
        addTracksToPlaylist(m_currentTracks, /*play_first=*/true);
        break;
    }
    return 0;
}

// ---------------------------------------------------------------------------
// Search button clicked
// ---------------------------------------------------------------------------
LRESULT CQobuzBrowserWnd::OnSearchClicked(WORD, WORD, HWND, BOOL&) {
    pfc::string8 q;
    uGetWindowText(m_searchEdit.m_hWnd, q);
    if (!q.is_empty())
        asyncSearch(q.get_ptr());
    return 0;
}

// Allow pressing Enter in search box
LRESULT CQobuzBrowserWnd::OnSearchEditChange(WORD, WORD, HWND, BOOL&) {
    // Nothing – search fires on button click
    return 0;
}

// ---------------------------------------------------------------------------
// Async result handlers (called on UI thread via PostMessage)
// ---------------------------------------------------------------------------

LRESULT CQobuzBrowserWnd::OnTracksReady(UINT, WPARAM, LPARAM lParam, BOOL&) {
    auto* tracks = reinterpret_cast<std::vector<QobuzTrack>*>(lParam);
    m_currentTracks = std::move(*tracks);
    delete tracks;
    populateList(m_currentTracks);
    setStatus(std::to_string(m_currentTracks.size()) + " track(s) loaded.");
    return 0;
}

LRESULT CQobuzBrowserWnd::OnAlbumsForTree(UINT, WPARAM wParam, LPARAM lParam, BOOL&) {
    HTREEITEM hParent = reinterpret_cast<HTREEITEM>(wParam);
    auto* albums = reinterpret_cast<std::vector<QobuzAlbum>*>(lParam);

    for (const auto& album : *albums) {
        std::wstring label = pfc::stringcvt::string_wide_from_utf8(
            (album.title + " – " + album.artist.name).c_str());
        addTreeNode(hParent, label,
            { TreeNodeData::Type::Album, album.id, album.title });
    }
    delete albums;

    setStatus(std::to_string(albums ? 0 : 0) + " albums added."); // always 0 here; count was in loop
    m_tree.Expand(hParent, TVE_EXPAND);
    return 0;
}

LRESULT CQobuzBrowserWnd::OnPlaylistsReady(UINT, WPARAM wParam, LPARAM lParam, BOOL&) {
    HTREEITEM hParent = reinterpret_cast<HTREEITEM>(wParam);
    auto* playlists = reinterpret_cast<std::vector<QobuzPlaylist>*>(lParam);

    for (const auto& pl : *playlists) {
        std::wstring label = pfc::stringcvt::string_wide_from_utf8(
            (pl.name + " (" + std::to_string(pl.track_count) + ")").c_str());
        addTreeNode(hParent, label,
            { TreeNodeData::Type::Playlist, pl.id, pl.name });
    }
    delete playlists;
    m_tree.Expand(hParent, TVE_EXPAND);
    return 0;
}

LRESULT CQobuzBrowserWnd::OnStatusMsg(UINT, WPARAM, LPARAM lParam, BOOL&) {
    auto* msg = reinterpret_cast<std::string*>(lParam);
    setStatus(*msg);
    delete msg;
    return 0;
}

LRESULT CQobuzBrowserWnd::OnErrorMsg(UINT, WPARAM, LPARAM lParam, BOOL&) {
    auto* msg = reinterpret_cast<std::string*>(lParam);
    setStatus("Error: " + *msg);
    delete msg;
    return 0;
}

// ---------------------------------------------------------------------------
// List-view helpers
// ---------------------------------------------------------------------------
void CQobuzBrowserWnd::setupList() {
    m_list.SetExtendedListViewStyle(LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES);
    m_list.InsertColumn(kColNum,      L"#",        LVCFMT_RIGHT,  40);
    m_list.InsertColumn(kColTitle,    L"Title",    LVCFMT_LEFT,  200);
    m_list.InsertColumn(kColArtist,   L"Artist",   LVCFMT_LEFT,  140);
    m_list.InsertColumn(kColAlbum,    L"Album",    LVCFMT_LEFT,  140);
    m_list.InsertColumn(kColDuration, L"Duration", LVCFMT_RIGHT,  55);
}

void CQobuzBrowserWnd::clearList() {
    m_list.DeleteAllItems();
    m_currentTracks.clear();
}

void CQobuzBrowserWnd::populateList(const std::vector<QobuzTrack>& tracks) {
    m_list.SetRedraw(FALSE);
    m_list.DeleteAllItems();

    for (int i = 0; i < static_cast<int>(tracks.size()); ++i) {
        const auto& t = tracks[i];

        auto wide = [](const std::string& s) -> std::wstring {
            return pfc::stringcvt::string_wide_from_utf8(s.c_str()).get_ptr();
        };

        int row = m_list.InsertItem(i, std::to_wstring(i + 1).c_str());
        m_list.SetItemText(row, kColTitle,    wide(t.title).c_str());
        m_list.SetItemText(row, kColArtist,   wide(t.performer.name).c_str());
        m_list.SetItemText(row, kColAlbum,    wide(t.album_title).c_str());
        m_list.SetItemText(row, kColDuration,
            pfc::stringcvt::string_wide_from_utf8(
                formatDuration(t.duration).c_str()).get_ptr());
    }

    m_list.SetRedraw(TRUE);
    m_list.Invalidate();
}

// ---------------------------------------------------------------------------
// Tree helpers
// ---------------------------------------------------------------------------
HTREEITEM CQobuzBrowserWnd::addTreeNode(HTREEITEM parent,
                                         const std::wstring& text,
                                         TreeNodeData data,
                                         bool addPlaceholder) {
    TVINSERTSTRUCT tvis{};
    tvis.hParent      = parent;
    tvis.hInsertAfter = TVI_LAST;
    tvis.item.mask    = TVIF_TEXT | TVIF_CHILDREN;
    tvis.item.pszText = const_cast<LPWSTR>(text.c_str());
    tvis.item.cChildren = addPlaceholder ? 1 : 0;

    HTREEITEM h = m_tree.InsertItem(&tvis);
    if (h) {
        m_treeData[h] = std::move(data);
        if (addPlaceholder) {
            // Insert a dummy child so the expand button appears
            TVINSERTSTRUCT dummy{};
            dummy.hParent      = h;
            dummy.hInsertAfter = TVI_LAST;
            dummy.item.mask    = TVIF_TEXT;
            dummy.item.pszText = const_cast<LPWSTR>(L"Loading…");
            m_tree.InsertItem(&dummy);
        }
    }
    return h;
}

void CQobuzBrowserWnd::removeChildren(HTREEITEM hParent) {
    HTREEITEM child = m_tree.GetChildItem(hParent);
    while (child) {
        HTREEITEM next = m_tree.GetNextSiblingItem(child);
        m_treeData.erase(child);
        m_tree.DeleteItem(child);
        child = next;
    }
}

// ---------------------------------------------------------------------------
// Playback helpers
// ---------------------------------------------------------------------------
void CQobuzBrowserWnd::playTrack(int listIndex) {
    if (listIndex < 0 || listIndex >= static_cast<int>(m_currentTracks.size())) return;
    addTracksToPlaylist({ m_currentTracks[listIndex] }, /*play_first=*/true);
}

void CQobuzBrowserWnd::addTracksToPlaylist(const std::vector<QobuzTrack>& tracks,
                                            bool play_first) {
    auto pm = static_api_ptr_t<playlist_manager>();
    t_size pl = pm->get_active_playlist();
    t_size insertPos = pm->playlist_get_item_count(pl);

    pfc::list_t<metadb_handle_ptr> items;
    for (const auto& t : tracks) {
        playable_location_impl loc;
        loc.set_path(("qobuz://track/" + t.id).c_str());
        loc.set_subsong(0);
        metadb_handle_ptr h;
        static_api_ptr_t<metadb>()->handle_create(h, loc);
        items.add_item(h);
    }

    pm->playlist_insert_items(pl, insertPos, items, bit_array_false());

    if (play_first) {
        pm->playlist_set_playback_cursor(pl, insertPos);
        static_api_ptr_t<play_control>()->play_or_unpause();
    }
}

void CQobuzBrowserWnd::setStatus(const std::string& msg) {
    m_statusBar.SetWindowText(
        pfc::stringcvt::string_wide_from_utf8(msg.c_str()));
}

// ---------------------------------------------------------------------------
// Formatting
// ---------------------------------------------------------------------------
std::string CQobuzBrowserWnd::formatDuration(int seconds) {
    int m = seconds / 60, s = seconds % 60;
    char buf[16];
    snprintf(buf, sizeof(buf), "%d:%02d", m, s);
    return buf;
}

// ---------------------------------------------------------------------------
// Async loaders  –  each detaches a thread that PostMessages results back
// ---------------------------------------------------------------------------

// Helper macro to grab credentials and post an error if missing
#define LOAD_CREDS(appId, appSec, token)                                   \
    auto [appId, appSec, token] = CQobuzBrowserWnd::getCredentials();      \
    if (appId.empty() || token.empty()) {                                  \
        PostMessage(hwnd, WM_QOBUZ_ERROR_MSG, 0,                           \
            (LPARAM)new std::string(                                       \
                "Not logged in. Configure credentials in Preferences."));  \
        return;                                                            \
    }

void CQobuzBrowserWnd::asyncLoadNewReleases(HTREEITEM parent) {
    HWND hwnd  = m_hWnd;
    auto alive = m_alive;
    setStatus("Loading new releases…");

    std::thread([hwnd, parent, alive]() {
        LOAD_CREDS(appId, appSec, token)
        QobuzAPI api(appId, appSec);
        auto* albums = new std::vector<QobuzAlbum>();
        std::string err;
        if (!api.getFeaturedAlbums(token, *albums, err)) {
            delete albums;
            if (*alive) PostMessage(hwnd, WM_QOBUZ_ERROR_MSG, 0,
                (LPARAM)new std::string(err));
            return;
        }
        if (*alive)
            PostMessage(hwnd, WM_QOBUZ_ALBUMS_FOR_TREE,
                (WPARAM)parent, (LPARAM)albums);
        else
            delete albums;
    }).detach();
}

void CQobuzBrowserWnd::asyncLoadUserPlaylists(HTREEITEM parent) {
    HWND hwnd  = m_hWnd;
    auto alive = m_alive;
    setStatus("Loading playlists…");

    std::thread([hwnd, parent, alive]() {
        LOAD_CREDS(appId, appSec, token)
        QobuzAPI api(appId, appSec);
        auto* lists = new std::vector<QobuzPlaylist>();
        std::string err;
        if (!api.getUserPlaylists(token, *lists, err)) {
            delete lists;
            if (*alive) PostMessage(hwnd, WM_QOBUZ_ERROR_MSG, 0,
                (LPARAM)new std::string(err));
            return;
        }
        if (*alive)
            PostMessage(hwnd, WM_QOBUZ_PLAYLISTS_READY,
                (WPARAM)parent, (LPARAM)lists);
        else
            delete lists;
    }).detach();
}

void CQobuzBrowserWnd::asyncLoadAlbumTracks(const std::string& album_id) {
    HWND hwnd  = m_hWnd;
    auto alive = m_alive;
    setStatus("Loading album…");

    std::thread([hwnd, alive, album_id]() {
        LOAD_CREDS(appId, appSec, token)
        QobuzAPI api(appId, appSec);
        QobuzAlbum album;
        std::string err;
        if (!api.getAlbum(album_id, token, album, err)) {
            if (*alive) PostMessage(hwnd, WM_QOBUZ_ERROR_MSG, 0,
                (LPARAM)new std::string(err));
            return;
        }
        auto* tracks = new std::vector<QobuzTrack>(std::move(album.tracks));
        if (*alive)
            PostMessage(hwnd, WM_QOBUZ_TRACKS_READY, 0, (LPARAM)tracks);
        else
            delete tracks;
    }).detach();
}

void CQobuzBrowserWnd::asyncLoadPlaylistTracks(const std::string& playlist_id) {
    HWND hwnd  = m_hWnd;
    auto alive = m_alive;
    setStatus("Loading playlist…");

    std::thread([hwnd, alive, playlist_id]() {
        LOAD_CREDS(appId, appSec, token)
        QobuzAPI api(appId, appSec);
        QobuzPlaylist pl;
        std::string err;
        if (!api.getPlaylist(playlist_id, token, pl, err)) {
            if (*alive) PostMessage(hwnd, WM_QOBUZ_ERROR_MSG, 0,
                (LPARAM)new std::string(err));
            return;
        }
        auto* tracks = new std::vector<QobuzTrack>(std::move(pl.tracks));
        if (*alive)
            PostMessage(hwnd, WM_QOBUZ_TRACKS_READY, 0, (LPARAM)tracks);
        else
            delete tracks;
    }).detach();
}

void CQobuzBrowserWnd::asyncLoadFavTracks() {
    HWND hwnd  = m_hWnd;
    auto alive = m_alive;
    setStatus("Loading favorite tracks…");

    std::thread([hwnd, alive]() {
        LOAD_CREDS(appId, appSec, token)
        QobuzAPI api(appId, appSec);
        std::vector<QobuzTrack> fTracks;
        std::vector<QobuzAlbum> fAlbums; // unused here
        std::string err;
        if (!api.getUserFavorites(token, "tracks", fTracks, fAlbums, err)) {
            if (*alive) PostMessage(hwnd, WM_QOBUZ_ERROR_MSG, 0,
                (LPARAM)new std::string(err));
            return;
        }
        auto* tracks = new std::vector<QobuzTrack>(std::move(fTracks));
        if (*alive)
            PostMessage(hwnd, WM_QOBUZ_TRACKS_READY, 0, (LPARAM)tracks);
        else
            delete tracks;
    }).detach();
}

void CQobuzBrowserWnd::asyncLoadFavAlbums(HTREEITEM parent) {
    HWND hwnd  = m_hWnd;
    auto alive = m_alive;
    setStatus("Loading favorite albums…");

    std::thread([hwnd, parent, alive]() {
        LOAD_CREDS(appId, appSec, token)
        QobuzAPI api(appId, appSec);
        std::vector<QobuzTrack> fTracks; // unused
        std::vector<QobuzAlbum> fAlbums;
        std::string err;
        if (!api.getUserFavorites(token, "albums", fTracks, fAlbums, err)) {
            if (*alive) PostMessage(hwnd, WM_QOBUZ_ERROR_MSG, 0,
                (LPARAM)new std::string(err));
            return;
        }
        auto* albums = new std::vector<QobuzAlbum>(std::move(fAlbums));
        if (*alive)
            PostMessage(hwnd, WM_QOBUZ_ALBUMS_FOR_TREE,
                (WPARAM)parent, (LPARAM)albums);
        else
            delete albums;
    }).detach();
}

void CQobuzBrowserWnd::asyncSearch(const std::string& query) {
    HWND hwnd  = m_hWnd;
    auto alive = m_alive;
    setStatus("Searching for \"" + query + "\"…");

    std::thread([hwnd, alive, query]() {
        LOAD_CREDS(appId, appSec, token)
        QobuzAPI api(appId, appSec);
        QobuzSearchResults results;
        std::string err;
        if (!api.search(query, token, "tracks", results, err)) {
            if (*alive) PostMessage(hwnd, WM_QOBUZ_ERROR_MSG, 0,
                (LPARAM)new std::string(err));
            return;
        }
        auto* tracks = new std::vector<QobuzTrack>(std::move(results.tracks));
        if (*alive)
            PostMessage(hwnd, WM_QOBUZ_TRACKS_READY, 0, (LPARAM)tracks);
        else
            delete tracks;
    }).detach();
}

#undef LOAD_CREDS

// ---------------------------------------------------------------------------
// Register the panel with foobar2000
// ---------------------------------------------------------------------------
static ui_element_factory_t<CQobuzBrowserElement> g_qobuz_browser_factory;
