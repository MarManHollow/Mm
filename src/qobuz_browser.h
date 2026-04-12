#pragma once
#include "stdafx.h"
#include "qobuz_api.h"
#include <map>
#include <thread>
#include <atomic>
#include <memory>

// ---------------------------------------------------------------------------
// Private window messages used to marshal async API results to the UI thread
// ---------------------------------------------------------------------------
#define WM_QOBUZ_TRACKS_READY     (WM_APP + 100) // lParam = new vector<QobuzTrack>*
#define WM_QOBUZ_ALBUMS_FOR_TREE  (WM_APP + 101) // wParam = HTREEITEM parent, lParam = new vector<QobuzAlbum>*
#define WM_QOBUZ_PLAYLISTS_READY  (WM_APP + 102) // wParam = HTREEITEM parent, lParam = new vector<QobuzPlaylist>*
#define WM_QOBUZ_STATUS_MSG       (WM_APP + 103) // lParam = new std::string*
#define WM_QOBUZ_ERROR_MSG        (WM_APP + 104) // lParam = new std::string*

// Control child IDs (no dialog template – created in WM_CREATE)
#define IDC_QBROWSER_TREE     2000
#define IDC_QBROWSER_LIST     2001
#define IDC_QBROWSER_SEARCH   2002
#define IDC_QBROWSER_BTN_GO   2003

// List-view column indices
enum BrowserCol { kColNum = 0, kColTitle, kColArtist, kColAlbum, kColDuration, kColCount };

// ---------------------------------------------------------------------------
// Per-node data for every HTREEITEM in the navigation tree
// ---------------------------------------------------------------------------
struct TreeNodeData {
    enum class Type {
        NewReleasesRoot,  // shows featured albums as children
        PlaylistsRoot,    // shows user playlists as children
        FavTracksRoot,    // loads favorite tracks into list
        FavAlbumsRoot,    // shows favorite albums as children
        Album,            // id = album_id  → loads album tracks
        Playlist,         // id = playlist_id → loads playlist tracks
    };
    Type        type;
    std::string id;           // album_id or playlist_id when applicable
    std::string display_name; // shown in tree
    bool        children_loaded = false;
};

// ---------------------------------------------------------------------------
// CQobuzBrowserWnd
//   ATL window embedded as a foobar2000 ui_element panel.
//   Layout: [search bar (top)] [tree (left) | list (right)]
// ---------------------------------------------------------------------------
class CQobuzBrowserWnd
    : public CWindowImpl<CQobuzBrowserWnd, CWindow,
                         CWinTraits<WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN, 0>>
{
public:
    DECLARE_WND_CLASS_EX(L"QobuzBrowserWnd", CS_DBLCLKS, COLOR_WINDOW)

    // Called by the ui_element wrapper to supply the callback
    void set_callback(ui_element_instance_callback_ptr cb) { m_callback = cb; }

    BEGIN_MSG_MAP(CQobuzBrowserWnd)
        MESSAGE_HANDLER(WM_CREATE,  OnCreate)
        MESSAGE_HANDLER(WM_DESTROY, OnDestroy)
        MESSAGE_HANDLER(WM_SIZE,    OnSize)
        NOTIFY_CODE_HANDLER(TVN_ITEMEXPANDING, OnTreeItemExpanding)
        NOTIFY_CODE_HANDLER(TVN_SELCHANGED,    OnTreeSelChanged)
        NOTIFY_CODE_HANDLER(NM_DBLCLK,         OnListDblClick)
        NOTIFY_CODE_HANDLER(NM_RCLICK,         OnListRClick)
        COMMAND_HANDLER(IDC_QBROWSER_BTN_GO, BN_CLICKED, OnSearchClicked)
        COMMAND_HANDLER(IDC_QBROWSER_SEARCH, EN_CHANGE,  OnSearchEditChange)
        MESSAGE_HANDLER(WM_QOBUZ_TRACKS_READY,    OnTracksReady)
        MESSAGE_HANDLER(WM_QOBUZ_ALBUMS_FOR_TREE, OnAlbumsForTree)
        MESSAGE_HANDLER(WM_QOBUZ_PLAYLISTS_READY, OnPlaylistsReady)
        MESSAGE_HANDLER(WM_QOBUZ_STATUS_MSG,      OnStatusMsg)
        MESSAGE_HANDLER(WM_QOBUZ_ERROR_MSG,       OnErrorMsg)
    END_MSG_MAP()

private:
    // --- child controls ---
    CTreeViewCtrl m_tree;
    CListViewCtrl m_list;
    CEdit         m_searchEdit;
    CButton       m_searchBtn;
    CStatic       m_statusBar;

    // --- tree item data ---
    std::map<HTREEITEM, TreeNodeData> m_treeData;
    // Root nodes (needed to add placeholder children)
    HTREEITEM m_hNewReleases = nullptr;
    HTREEITEM m_hPlaylists   = nullptr;
    HTREEITEM m_hFavTracks   = nullptr;
    HTREEITEM m_hFavAlbums   = nullptr;

    // --- currently displayed tracks (for playback) ---
    std::vector<QobuzTrack> m_currentTracks;

    // --- async safety ---
    std::shared_ptr<std::atomic<bool>> m_alive;

    // --- foobar2000 callback (for colour / DPI notifications) ---
    ui_element_instance_callback_ptr m_callback;

    // --- message handlers ---
    LRESULT OnCreate (UINT, WPARAM, LPARAM, BOOL&);
    LRESULT OnDestroy(UINT, WPARAM, LPARAM, BOOL&);
    LRESULT OnSize   (UINT, WPARAM, LPARAM, BOOL&);

    LRESULT OnTreeItemExpanding(int, LPNMHDR, BOOL&);
    LRESULT OnTreeSelChanged   (int, LPNMHDR, BOOL&);
    LRESULT OnListDblClick     (int, LPNMHDR, BOOL&);
    LRESULT OnListRClick       (int, LPNMHDR, BOOL&);

    LRESULT OnSearchClicked   (WORD, WORD, HWND, BOOL&);
    LRESULT OnSearchEditChange(WORD, WORD, HWND, BOOL&);

    LRESULT OnTracksReady   (UINT, WPARAM, LPARAM, BOOL&);
    LRESULT OnAlbumsForTree (UINT, WPARAM, LPARAM, BOOL&);
    LRESULT OnPlaylistsReady(UINT, WPARAM, LPARAM, BOOL&);
    LRESULT OnStatusMsg     (UINT, WPARAM, LPARAM, BOOL&);
    LRESULT OnErrorMsg      (UINT, WPARAM, LPARAM, BOOL&);

    // --- helpers ---
    void setupList();
    void clearList();
    void populateList(const std::vector<QobuzTrack>& tracks);

    HTREEITEM addTreeNode(HTREEITEM parent, const std::wstring& text,
                          TreeNodeData data, bool addPlaceholder = false);
    void removeChildren(HTREEITEM parent);

    void playTrack(int listIndex);
    void addTracksToPlaylist(const std::vector<QobuzTrack>& tracks, bool play_first);

    void setStatus(const std::string& msg);

    // Async launchers (detach a thread that PostMessages results back)
    void asyncLoadNewReleases(HTREEITEM parent);
    void asyncLoadUserPlaylists(HTREEITEM parent);
    void asyncLoadAlbumTracks(const std::string& album_id);
    void asyncLoadPlaylistTracks(const std::string& playlist_id);
    void asyncLoadFavTracks();
    void asyncLoadFavAlbums(HTREEITEM parent);
    void asyncSearch(const std::string& query);

    static std::string formatDuration(int seconds);

    // Return current app_id/secret/token, empty strings if not configured
    static std::tuple<std::string,std::string,std::string> getCredentials();
};

// ---------------------------------------------------------------------------
// foobar2000 ui_element wrapper  (registers the panel in the layout system)
// ---------------------------------------------------------------------------
class CQobuzBrowserElement
    : public ui_element_impl_withpopup<CQobuzBrowserWnd>
{
public:
    static GUID g_get_guid() {
        // {D5E6F7A8-CCCC-4321-ABCD-000000000002}
        static const GUID g = {
            0xd5e6f7a8, 0xcccc, 0x4321,
            {0xab,0xcd,0x00,0x00,0x00,0x00,0x00,0x02}
        };
        return g;
    }
    static void g_get_name(pfc::string_base& out) { out = "Qobuz Browser"; }
    static ui_element_config::ptr g_get_default_configuration() {
        return ui_element_config::g_create_empty(g_get_guid());
    }
    static const char* g_get_description() {
        return "Browse new releases, playlists, and favorites from Qobuz.";
    }
};
