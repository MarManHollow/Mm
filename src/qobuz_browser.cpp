#include "stdafx.h"
#include "qobuz_browser.h"
#include "config.h"

// ---------------------------------------------------------------------------
// Credential helper
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

    HFONT hFont = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
    HINSTANCE hInst = GetModuleHandle(nullptr);

    // Search edit
    m_searchEdit = CreateWindowExW(0, L"EDIT", L"",
        WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL,
        0, 0, 10, 10, m_hWnd, (HMENU)(UINT_PTR)IDC_QBROWSER_SEARCH, hInst, nullptr);
    SendMessage(m_searchEdit, WM_SETFONT, (WPARAM)hFont, TRUE);

    // Search button
    m_searchBtn = CreateWindowExW(0, L"BUTTON", L"Search",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        0, 0, 10, 10, m_hWnd, (HMENU)(UINT_PTR)IDC_QBROWSER_BTN_GO, hInst, nullptr);
    SendMessage(m_searchBtn, WM_SETFONT, (WPARAM)hFont, TRUE);

    // Navigation tree
    m_tree = CreateWindowExW(0, WC_TREEVIEWW, L"",
        WS_CHILD | WS_VISIBLE | WS_BORDER |
        TVS_HASLINES | TVS_HASBUTTONS | TVS_LINESATROOT | TVS_SHOWSELALWAYS,
        0, 0, 10, 10, m_hWnd, (HMENU)(UINT_PTR)IDC_QBROWSER_TREE, hInst, nullptr);

    // Content list
    m_list = CreateWindowExW(0, WC_LISTVIEWW, L"",
        WS_CHILD | WS_VISIBLE | WS_BORDER |
        LVS_REPORT | LVS_SHOWSELALWAYS | LVS_SINGLESEL,
        0, 0, 10, 10, m_hWnd, (HMENU)(UINT_PTR)IDC_QBROWSER_LIST, hInst, nullptr);
    ListView_SetExtendedListViewStyle(m_list,
        LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES);
    setupListColumns();

    // Status bar
    m_statusBar = CreateWindowExW(0, L"STATIC", L"",
        WS_CHILD | WS_VISIBLE | SS_LEFT,
        0, 0, 10, 10, m_hWnd, (HMENU)(UINT_PTR)IDC_QBROWSER_STATUS, hInst, nullptr);
    SendMessage(m_statusBar, WM_SETFONT, (WPARAM)hFont, TRUE);

    // ---- Populate tree root nodes ----
    m_hNewReleases = addTreeNode(TVI_ROOT, L"New Releases",
        { TreeNodeData::Type::NewReleasesRoot }, /*placeholder=*/true);

    m_hPlaylists = addTreeNode(TVI_ROOT, L"My Playlists",
        { TreeNodeData::Type::PlaylistsRoot }, /*placeholder=*/true);

    HTREEITEM hFav = addTreeNode(TVI_ROOT, L"Favorites",
        { TreeNodeData::Type::Root });
    m_hFavTracks = addTreeNode(hFav, L"Tracks",
        { TreeNodeData::Type::FavTracksRoot });
    m_hFavAlbums = addTreeNode(hFav, L"Albums",
        { TreeNodeData::Type::FavAlbumsRoot }, /*placeholder=*/true);
    TreeView_Expand(m_tree, hFav, TVE_EXPAND);

    setStatus("Select a section from the tree to browse.");
    return 0;
}

// ---------------------------------------------------------------------------
// WM_DESTROY
// ---------------------------------------------------------------------------
LRESULT CQobuzBrowserWnd::OnDestroy(UINT, WPARAM, LPARAM, BOOL&) {
    *m_alive = false;
    return 0;
}

// ---------------------------------------------------------------------------
// WM_SIZE  –  manual layout
// ---------------------------------------------------------------------------
LRESULT CQobuzBrowserWnd::OnSize(UINT, WPARAM, LPARAM lParam, BOOL&) {
    const int W = LOWORD(lParam), H = HIWORD(lParam);
    const int kSearchH = 24, kStatusH = 18, kBtnW = 60, kTreeW = 200, kPad = 4;

    ::SetWindowPos(m_searchEdit, nullptr,
        kPad, kPad, W - kBtnW - kPad * 3, kSearchH, SWP_NOZORDER);
    ::SetWindowPos(m_searchBtn, nullptr,
        W - kBtnW - kPad, kPad, kBtnW, kSearchH, SWP_NOZORDER);
    ::SetWindowPos(m_statusBar, nullptr,
        kPad, H - kStatusH - kPad, W - kPad * 2, kStatusH, SWP_NOZORDER);

    const int top = kSearchH + kPad * 2;
    const int ch  = H - top - kStatusH - kPad * 2;
    ::SetWindowPos(m_tree, nullptr, 0,           top, kTreeW,         ch, SWP_NOZORDER);
    ::SetWindowPos(m_list, nullptr, kTreeW + 1,  top, W - kTreeW - 1, ch, SWP_NOZORDER);

    // Proportional column widths
    const int lw = W - kTreeW - 1;
    if (lw > 100) {
        ListView_SetColumnWidth(m_list, kColNum,      40);
        ListView_SetColumnWidth(m_list, kColTitle,    lw / 3);
        ListView_SetColumnWidth(m_list, kColArtist,   lw / 4);
        ListView_SetColumnWidth(m_list, kColAlbum,    lw / 4);
        ListView_SetColumnWidth(m_list, kColDuration, 55);
    }
    return 0;
}

// ---------------------------------------------------------------------------
// WM_NOTIFY  –  handles tree + list notifications
// ---------------------------------------------------------------------------
LRESULT CQobuzBrowserWnd::OnNotify(UINT, WPARAM wParam, LPARAM lParam, BOOL&) {
    NMHDR* pnm = reinterpret_cast<NMHDR*>(lParam);

    // --- Tree notifications ---
    if (pnm->hwndFrom == m_tree) {
        if (pnm->code == TVN_ITEMEXPANDINGW) {
            auto* p = reinterpret_cast<NMTREEVIEWW*>(lParam);
            if (p->action == TVE_EXPAND) {
                HTREEITEM hItem = p->itemNew.hItem;
                auto it = m_treeData.find(hItem);
                if (it != m_treeData.end() && !it->second.children_loaded) {
                    it->second.children_loaded = true;
                    removeChildren(hItem);
                    switch (it->second.type) {
                    case TreeNodeData::Type::NewReleasesRoot:
                        asyncLoadNewReleases(hItem); break;
                    case TreeNodeData::Type::PlaylistsRoot:
                        asyncLoadUserPlaylists(hItem); break;
                    case TreeNodeData::Type::FavAlbumsRoot:
                        asyncLoadFavAlbums(hItem); break;
                    default: break;
                    }
                }
            }
        }
        else if (pnm->code == TVN_SELCHANGEDW) {
            auto* p = reinterpret_cast<NMTREEVIEWW*>(lParam);
            HTREEITEM hItem = p->itemNew.hItem;
            auto it = m_treeData.find(hItem);
            if (it != m_treeData.end()) {
                const auto& nd = it->second;
                switch (nd.type) {
                case TreeNodeData::Type::Album:
                    asyncLoadAlbumTracks(nd.id); break;
                case TreeNodeData::Type::Playlist:
                    asyncLoadPlaylistTracks(nd.id); break;
                case TreeNodeData::Type::FavTracksRoot:
                    asyncLoadFavTracks(); break;
                default: break;
                }
            }
        }
    }

    // --- List notifications ---
    if (pnm->hwndFrom == m_list) {
        if (pnm->code == NM_DBLCLK) {
            int sel = ListView_GetNextItem(m_list, -1, LVNI_SELECTED);
            if (sel >= 0 && sel < (int)m_currentTracks.size())
                playTrack(sel);
        }
        else if (pnm->code == NM_RCLICK) {
            int sel = ListView_GetNextItem(m_list, -1, LVNI_SELECTED);
            if (sel >= 0 && sel < (int)m_currentTracks.size()) {
                POINT pt; GetCursorPos(&pt);
                HMENU hMenu = CreatePopupMenu();
                AppendMenuW(hMenu, MF_STRING, 1, L"Play now");
                AppendMenuW(hMenu, MF_STRING, 2, L"Add to queue (end of playlist)");
                AppendMenuW(hMenu, MF_STRING, 3, L"Play all in list");
                int cmd = TrackPopupMenu(hMenu,
                    TPM_RETURNCMD | TPM_RIGHTBUTTON, pt.x, pt.y, 0, m_hWnd, nullptr);
                DestroyMenu(hMenu);
                switch (cmd) {
                case 1: playTrack(sel); break;
                case 2: addTracksToPlaylist({ m_currentTracks[sel] }, false); break;
                case 3: addTracksToPlaylist(m_currentTracks, true); break;
                }
            }
        }
    }
    return 0;
}

// ---------------------------------------------------------------------------
// Search button
// ---------------------------------------------------------------------------
LRESULT CQobuzBrowserWnd::OnSearchClicked(WORD, WORD, HWND, BOOL&) {
    wchar_t buf[512] = {};
    ::GetWindowTextW(m_searchEdit, buf, 512);
    if (buf[0] != L'\0') {
        pfc::stringcvt::string_utf8_from_wide conv(buf, wcslen(buf));
        asyncSearch(conv.get_ptr());
    }
    return 0;
}

LRESULT CQobuzBrowserWnd::OnSearchEditChange(WORD, WORD, HWND, BOOL&) {
    return 0; // search on button click only
}

// ---------------------------------------------------------------------------
// Async result handlers (UI thread)
// ---------------------------------------------------------------------------
LRESULT CQobuzBrowserWnd::OnTracksReady(UINT, WPARAM, LPARAM lParam, BOOL&) {
    auto* tracks = reinterpret_cast<std::vector<QobuzTrack>*>(lParam);
    m_currentTracks = std::move(*tracks);
    delete tracks;
    populateList(m_currentTracks);
    std::string msg = std::to_string(m_currentTracks.size()) + " track(s) loaded.";
    setStatus(msg.c_str());
    return 0;
}

LRESULT CQobuzBrowserWnd::OnAlbumsForTree(UINT, WPARAM wParam, LPARAM lParam, BOOL&) {
    HTREEITEM hParent = reinterpret_cast<HTREEITEM>(wParam);
    auto* albums = reinterpret_cast<std::vector<QobuzAlbum>*>(lParam);
    for (const auto& album : *albums) {
        std::string label = album.title + " \xe2\x80\x93 " + album.artist.name;
        pfc::stringcvt::string_wide_from_utf8 wlabel(label.c_str());
        addTreeNode(hParent, wlabel, { TreeNodeData::Type::Album, album.id });
    }
    delete albums;
    TreeView_Expand(m_tree, hParent, TVE_EXPAND);
    setStatus("Albums loaded.");
    return 0;
}

LRESULT CQobuzBrowserWnd::OnPlaylistsReady(UINT, WPARAM wParam, LPARAM lParam, BOOL&) {
    HTREEITEM hParent = reinterpret_cast<HTREEITEM>(wParam);
    auto* playlists = reinterpret_cast<std::vector<QobuzPlaylist>*>(lParam);
    for (const auto& pl : *playlists) {
        std::string label = pl.name + " (" + std::to_string(pl.track_count) + ")";
        pfc::stringcvt::string_wide_from_utf8 wlabel(label.c_str());
        addTreeNode(hParent, wlabel, { TreeNodeData::Type::Playlist, pl.id });
    }
    delete playlists;
    TreeView_Expand(m_tree, hParent, TVE_EXPAND);
    setStatus("Playlists loaded.");
    return 0;
}

LRESULT CQobuzBrowserWnd::OnStatusMsg(UINT, WPARAM, LPARAM lParam, BOOL&) {
    auto* msg = reinterpret_cast<std::string*>(lParam);
    setStatus(msg->c_str());
    delete msg;
    return 0;
}

LRESULT CQobuzBrowserWnd::OnErrorMsg(UINT, WPARAM, LPARAM lParam, BOOL&) {
    auto* msg = reinterpret_cast<std::string*>(lParam);
    setStatus(("Error: " + *msg).c_str());
    delete msg;
    return 0;
}

// ---------------------------------------------------------------------------
// List helpers
// ---------------------------------------------------------------------------
void CQobuzBrowserWnd::setupListColumns() {
    auto addCol = [&](int idx, const wchar_t* title, int w, int fmt = LVCFMT_LEFT) {
        LVCOLUMNW lvc = {};
        lvc.mask    = LVCF_TEXT | LVCF_WIDTH | LVCF_FMT;
        lvc.fmt     = fmt;
        lvc.cx      = w;
        lvc.pszText = const_cast<LPWSTR>(title);
        ListView_InsertColumn(m_list, idx, &lvc);
    };
    addCol(kColNum,      L"#",        40, LVCFMT_RIGHT);
    addCol(kColTitle,    L"Title",    200);
    addCol(kColArtist,   L"Artist",   140);
    addCol(kColAlbum,    L"Album",    140);
    addCol(kColDuration, L"Duration",  55, LVCFMT_RIGHT);
}

void CQobuzBrowserWnd::populateList(const std::vector<QobuzTrack>& tracks) {
    SendMessage(m_list, WM_SETREDRAW, FALSE, 0);
    ListView_DeleteAllItems(m_list);

    for (int i = 0; i < (int)tracks.size(); ++i) {
        const auto& t = tracks[i];

        // Row number
        std::wstring numW = std::to_wstring(i + 1);
        LVITEMW lvi = {};
        lvi.mask     = LVIF_TEXT;
        lvi.iItem    = i;
        lvi.iSubItem = 0;
        lvi.pszText  = const_cast<LPWSTR>(numW.c_str());
        int row = ListView_InsertItem(m_list, &lvi);

        auto setCol = [&](int col, const std::string& utf8) {
            pfc::stringcvt::string_wide_from_utf8 w(utf8.c_str());
            ListView_SetItemText(m_list, row, col,
                const_cast<LPWSTR>(w.get_ptr()));
        };

        setCol(kColTitle,    t.title);
        setCol(kColArtist,   t.performer.name);
        setCol(kColAlbum,    t.album_title);

        std::wstring durW = formatDuration(t.duration);
        ListView_SetItemText(m_list, row, kColDuration,
            const_cast<LPWSTR>(durW.c_str()));
    }

    SendMessage(m_list, WM_SETREDRAW, TRUE, 0);
    ::InvalidateRect(m_list, nullptr, FALSE);
}

// ---------------------------------------------------------------------------
// Tree helpers
// ---------------------------------------------------------------------------
HTREEITEM CQobuzBrowserWnd::addTreeNode(HTREEITEM parent, const wchar_t* text,
                                         TreeNodeData data, bool addPlaceholder) {
    TVINSERTSTRUCTW tvis = {};
    tvis.hParent      = parent;
    tvis.hInsertAfter = TVI_LAST;
    tvis.item.mask    = TVIF_TEXT | TVIF_CHILDREN;
    tvis.item.pszText = const_cast<LPWSTR>(text);
    tvis.item.cChildren = addPlaceholder ? 1 : 0;

    HTREEITEM h = TreeView_InsertItem(m_tree, &tvis);
    if (h) {
        m_treeData[h] = std::move(data);
        if (addPlaceholder) {
            TVINSERTSTRUCTW dummy = {};
            dummy.hParent      = h;
            dummy.hInsertAfter = TVI_LAST;
            dummy.item.mask    = TVIF_TEXT;
            dummy.item.pszText = const_cast<LPWSTR>(L"Loading\u2026");
            TreeView_InsertItem(m_tree, &dummy);
        }
    }
    return h;
}

void CQobuzBrowserWnd::removeChildren(HTREEITEM hParent) {
    HTREEITEM child = TreeView_GetChild(m_tree, hParent);
    while (child) {
        HTREEITEM next = TreeView_GetNextSibling(m_tree, child);
        m_treeData.erase(child);
        TreeView_DeleteItem(m_tree, child);
        child = next;
    }
}

// ---------------------------------------------------------------------------
// Playback
// ---------------------------------------------------------------------------
void CQobuzBrowserWnd::playTrack(int listIndex) {
    if (listIndex < 0 || listIndex >= (int)m_currentTracks.size()) return;
    addTracksToPlaylist({ m_currentTracks[listIndex] }, true);
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

    if (play_first)
        pm->playlist_execute_default_action(pl, insertPos);
}

void CQobuzBrowserWnd::setStatus(const char* msg) {
    pfc::stringcvt::string_wide_from_utf8 w(msg);
    ::SetWindowTextW(m_statusBar, w);
}

// ---------------------------------------------------------------------------
// Formatting
// ---------------------------------------------------------------------------
std::wstring CQobuzBrowserWnd::formatDuration(int seconds) {
    wchar_t buf[16];
    swprintf_s(buf, L"%d:%02d", seconds / 60, seconds % 60);
    return buf;
}

// ---------------------------------------------------------------------------
// Async loaders
// ---------------------------------------------------------------------------
#define LOAD_CREDS(appId, appSec, token)                                        \
    auto [appId, appSec, token] = CQobuzBrowserWnd::getCredentials();           \
    if (appId.empty() || token.empty()) {                                       \
        ::PostMessage(hwnd, WM_QOBUZ_ERROR_MSG, 0,                              \
            (LPARAM)new std::string(                                            \
                "Not logged in. Configure credentials in Preferences."));       \
        return;                                                                 \
    }

void CQobuzBrowserWnd::asyncLoadNewReleases(HTREEITEM parent) {
    HWND hwnd = m_hWnd; auto alive = m_alive;
    setStatus("Loading new releases\xe2\x80\xa6");
    std::thread([hwnd, parent, alive]() {
        LOAD_CREDS(appId, appSec, token)
        QobuzAPI api(appId, appSec);
        auto* albums = new std::vector<QobuzAlbum>();
        std::string err;
        if (!api.getFeaturedAlbums(token, *albums, err)) {
            delete albums;
            if (*alive) ::PostMessage(hwnd, WM_QOBUZ_ERROR_MSG, 0,
                (LPARAM)new std::string(err));
            return;
        }
        if (*alive)
            ::PostMessage(hwnd, WM_QOBUZ_ALBUMS_FOR_TREE, (WPARAM)parent, (LPARAM)albums);
        else delete albums;
    }).detach();
}

void CQobuzBrowserWnd::asyncLoadUserPlaylists(HTREEITEM parent) {
    HWND hwnd = m_hWnd; auto alive = m_alive;
    setStatus("Loading playlists\xe2\x80\xa6");
    std::thread([hwnd, parent, alive]() {
        LOAD_CREDS(appId, appSec, token)
        QobuzAPI api(appId, appSec);
        auto* lists = new std::vector<QobuzPlaylist>();
        std::string err;
        if (!api.getUserPlaylists(token, *lists, err)) {
            delete lists;
            if (*alive) ::PostMessage(hwnd, WM_QOBUZ_ERROR_MSG, 0,
                (LPARAM)new std::string(err));
            return;
        }
        if (*alive)
            ::PostMessage(hwnd, WM_QOBUZ_PLAYLISTS_READY, (WPARAM)parent, (LPARAM)lists);
        else delete lists;
    }).detach();
}

void CQobuzBrowserWnd::asyncLoadAlbumTracks(const std::string& album_id) {
    HWND hwnd = m_hWnd; auto alive = m_alive;
    setStatus("Loading album\xe2\x80\xa6");
    std::thread([hwnd, alive, album_id]() {
        LOAD_CREDS(appId, appSec, token)
        QobuzAPI api(appId, appSec);
        QobuzAlbum album; std::string err;
        if (!api.getAlbum(album_id, token, album, err)) {
            if (*alive) ::PostMessage(hwnd, WM_QOBUZ_ERROR_MSG, 0,
                (LPARAM)new std::string(err));
            return;
        }
        auto* tracks = new std::vector<QobuzTrack>(std::move(album.tracks));
        if (*alive) ::PostMessage(hwnd, WM_QOBUZ_TRACKS_READY, 0, (LPARAM)tracks);
        else delete tracks;
    }).detach();
}

void CQobuzBrowserWnd::asyncLoadPlaylistTracks(const std::string& playlist_id) {
    HWND hwnd = m_hWnd; auto alive = m_alive;
    setStatus("Loading playlist\xe2\x80\xa6");
    std::thread([hwnd, alive, playlist_id]() {
        LOAD_CREDS(appId, appSec, token)
        QobuzAPI api(appId, appSec);
        QobuzPlaylist pl; std::string err;
        if (!api.getPlaylist(playlist_id, token, pl, err)) {
            if (*alive) ::PostMessage(hwnd, WM_QOBUZ_ERROR_MSG, 0,
                (LPARAM)new std::string(err));
            return;
        }
        auto* tracks = new std::vector<QobuzTrack>(std::move(pl.tracks));
        if (*alive) ::PostMessage(hwnd, WM_QOBUZ_TRACKS_READY, 0, (LPARAM)tracks);
        else delete tracks;
    }).detach();
}

void CQobuzBrowserWnd::asyncLoadFavTracks() {
    HWND hwnd = m_hWnd; auto alive = m_alive;
    setStatus("Loading favorite tracks\xe2\x80\xa6");
    std::thread([hwnd, alive]() {
        LOAD_CREDS(appId, appSec, token)
        QobuzAPI api(appId, appSec);
        std::vector<QobuzTrack> ft; std::vector<QobuzAlbum> fa;
        std::string err;
        if (!api.getUserFavorites(token, "tracks", ft, fa, err)) {
            if (*alive) ::PostMessage(hwnd, WM_QOBUZ_ERROR_MSG, 0,
                (LPARAM)new std::string(err));
            return;
        }
        auto* tracks = new std::vector<QobuzTrack>(std::move(ft));
        if (*alive) ::PostMessage(hwnd, WM_QOBUZ_TRACKS_READY, 0, (LPARAM)tracks);
        else delete tracks;
    }).detach();
}

void CQobuzBrowserWnd::asyncLoadFavAlbums(HTREEITEM parent) {
    HWND hwnd = m_hWnd; auto alive = m_alive;
    setStatus("Loading favorite albums\xe2\x80\xa6");
    std::thread([hwnd, parent, alive]() {
        LOAD_CREDS(appId, appSec, token)
        QobuzAPI api(appId, appSec);
        std::vector<QobuzTrack> ft; std::vector<QobuzAlbum> fa;
        std::string err;
        if (!api.getUserFavorites(token, "albums", ft, fa, err)) {
            if (*alive) ::PostMessage(hwnd, WM_QOBUZ_ERROR_MSG, 0,
                (LPARAM)new std::string(err));
            return;
        }
        auto* albums = new std::vector<QobuzAlbum>(std::move(fa));
        if (*alive)
            ::PostMessage(hwnd, WM_QOBUZ_ALBUMS_FOR_TREE, (WPARAM)parent, (LPARAM)albums);
        else delete albums;
    }).detach();
}

void CQobuzBrowserWnd::asyncSearch(const std::string& query) {
    HWND hwnd = m_hWnd; auto alive = m_alive;
    setStatus(("Searching for \"" + query + "\"\xe2\x80\xa6").c_str());
    std::thread([hwnd, alive, query]() {
        LOAD_CREDS(appId, appSec, token)
        QobuzAPI api(appId, appSec);
        QobuzSearchResults results; std::string err;
        if (!api.search(query, token, "tracks", results, err)) {
            if (*alive) ::PostMessage(hwnd, WM_QOBUZ_ERROR_MSG, 0,
                (LPARAM)new std::string(err));
            return;
        }
        auto* tracks = new std::vector<QobuzTrack>(std::move(results.tracks));
        if (*alive) ::PostMessage(hwnd, WM_QOBUZ_TRACKS_READY, 0, (LPARAM)tracks);
        else delete tracks;
    }).detach();
}

#undef LOAD_CREDS

// ---------------------------------------------------------------------------
// Register the panel
// ---------------------------------------------------------------------------
static service_factory_single_t<CQobuzBrowserElement> g_qobuz_browser_factory;
