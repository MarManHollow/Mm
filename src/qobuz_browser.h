#pragma once
#include "stdafx.h"
#include "qobuz_api.h"
#include <ui_element.h>
#include <map>
#include <thread>
#include <atomic>
#include <memory>

// ---------------------------------------------------------------------------
// Private window messages for async API results → UI thread
// ---------------------------------------------------------------------------
#define WM_QOBUZ_TRACKS_READY     (WM_APP + 100)
#define WM_QOBUZ_ALBUMS_FOR_TREE  (WM_APP + 101)
#define WM_QOBUZ_PLAYLISTS_READY  (WM_APP + 102)
#define WM_QOBUZ_STATUS_MSG       (WM_APP + 103)
#define WM_QOBUZ_ERROR_MSG        (WM_APP + 104)

// Child control IDs
#define IDC_QBROWSER_TREE     2000
#define IDC_QBROWSER_LIST     2001
#define IDC_QBROWSER_SEARCH   2002
#define IDC_QBROWSER_BTN_GO   2003
#define IDC_QBROWSER_STATUS   2010

// List-view column indices
enum BrowserCol { kColNum = 0, kColTitle, kColArtist, kColAlbum, kColDuration, kColCount };

// ---------------------------------------------------------------------------
// Per-node data for every HTREEITEM in the navigation tree
// ---------------------------------------------------------------------------
struct TreeNodeData {
    enum class Type {
        Root,          // generic non-clickable root
        NewReleasesRoot,
        PlaylistsRoot,
        FavTracksRoot,
        FavAlbumsRoot,
        Album,         // id = album_id
        Playlist,      // id = playlist_id
    };
    Type        type = Type::Root;
    std::string id;
    bool        children_loaded = false;
};

// ---------------------------------------------------------------------------
// CQobuzBrowserWnd  – ATL window, no WTL required
// Uses raw HWND + Win32 common-control macros for tree/list/edit/button.
// ---------------------------------------------------------------------------
class CQobuzBrowserWnd
    : public CWindowImpl<CQobuzBrowserWnd, CWindow,
                         CWinTraits<WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN, 0>>
{
public:
    DECLARE_WND_CLASS_EX(L"QobuzBrowserWnd", CS_DBLCLKS, COLOR_WINDOW)

    void set_callback(ui_element_instance_callback_ptr cb) { m_callback = cb; }

    BEGIN_MSG_MAP(CQobuzBrowserWnd)
        MESSAGE_HANDLER(WM_CREATE,  OnCreate)
        MESSAGE_HANDLER(WM_DESTROY, OnDestroy)
        MESSAGE_HANDLER(WM_SIZE,    OnSize)
        MESSAGE_HANDLER(WM_NOTIFY,  OnNotify)
        COMMAND_HANDLER(IDC_QBROWSER_BTN_GO, BN_CLICKED,  OnSearchClicked)
        COMMAND_HANDLER(IDC_QBROWSER_SEARCH, EN_CHANGE,   OnSearchEditChange)
        MESSAGE_HANDLER(WM_QOBUZ_TRACKS_READY,    OnTracksReady)
        MESSAGE_HANDLER(WM_QOBUZ_ALBUMS_FOR_TREE, OnAlbumsForTree)
        MESSAGE_HANDLER(WM_QOBUZ_PLAYLISTS_READY, OnPlaylistsReady)
        MESSAGE_HANDLER(WM_QOBUZ_STATUS_MSG,      OnStatusMsg)
        MESSAGE_HANDLER(WM_QOBUZ_ERROR_MSG,       OnErrorMsg)
    END_MSG_MAP()

private:
    // Raw child window handles (no WTL)
    HWND m_tree       = nullptr;
    HWND m_list       = nullptr;
    HWND m_searchEdit = nullptr;
    HWND m_searchBtn  = nullptr;
    HWND m_statusBar  = nullptr;

    // Tree item data
    std::map<HTREEITEM, TreeNodeData> m_treeData;
    HTREEITEM m_hNewReleases = nullptr;
    HTREEITEM m_hPlaylists   = nullptr;
    HTREEITEM m_hFavTracks   = nullptr;
    HTREEITEM m_hFavAlbums   = nullptr;

    // Currently displayed tracks
    std::vector<QobuzTrack> m_currentTracks;

    // Async safety flag
    std::shared_ptr<std::atomic<bool>> m_alive;

    ui_element_instance_callback_ptr m_callback;

    // --- Message handlers ---
    LRESULT OnCreate (UINT, WPARAM, LPARAM, BOOL&);
    LRESULT OnDestroy(UINT, WPARAM, LPARAM, BOOL&);
    LRESULT OnSize   (UINT, WPARAM, LPARAM, BOOL&);
    LRESULT OnNotify (UINT, WPARAM, LPARAM, BOOL&);

    LRESULT OnSearchClicked   (WORD, WORD, HWND, BOOL&);
    LRESULT OnSearchEditChange(WORD, WORD, HWND, BOOL&);

    LRESULT OnTracksReady   (UINT, WPARAM, LPARAM, BOOL&);
    LRESULT OnAlbumsForTree (UINT, WPARAM, LPARAM, BOOL&);
    LRESULT OnPlaylistsReady(UINT, WPARAM, LPARAM, BOOL&);
    LRESULT OnStatusMsg     (UINT, WPARAM, LPARAM, BOOL&);
    LRESULT OnErrorMsg      (UINT, WPARAM, LPARAM, BOOL&);

    // --- Helpers ---
    void setupListColumns();
    void populateList(const std::vector<QobuzTrack>& tracks);

    HTREEITEM addTreeNode(HTREEITEM parent, const wchar_t* text,
                          TreeNodeData data, bool addPlaceholder = false);
    void removeChildren(HTREEITEM parent);

    void playTrack(int listIndex);
    void addTracksToPlaylist(const std::vector<QobuzTrack>& tracks, bool play_first);
    void setStatus(const char* msg);

    // Async launchers
    void asyncLoadNewReleases(HTREEITEM parent);
    void asyncLoadUserPlaylists(HTREEITEM parent);
    void asyncLoadAlbumTracks(const std::string& album_id);
    void asyncLoadPlaylistTracks(const std::string& playlist_id);
    void asyncLoadFavTracks();
    void asyncLoadFavAlbums(HTREEITEM parent);
    void asyncSearch(const std::string& query);

    static std::wstring formatDuration(int seconds);
    static std::tuple<std::string,std::string,std::string> getCredentials();
};

// ---------------------------------------------------------------------------
// foobar2000 ui_element wrapper
// ---------------------------------------------------------------------------
class CQobuzBrowserElement
    : public ui_element_impl_withpopup<CQobuzBrowserWnd>
{
public:
    static GUID g_get_guid() {
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
