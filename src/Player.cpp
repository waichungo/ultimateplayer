#include <wx/wx.h>
// #include <wx/setup.h>
#include <wx/sizer.h>
#include <wx/filedlg.h>
#include <wx/panel.h>
#include <wx/button.h>
#include <wx/msgdlg.h>
#include <wx/overlay.h>
#include <wx/dcbuffer.h>
#include <wx/graphics.h>
#include <Windows.h>

extern "C"
{
#include <vlc/vlc.h>
}
#include "resources.h"

class TopBar : public wxPanel {
public:
	wxStaticText* title;
	wxPanel* dragPanel;
	wxButton* minimize;
	wxButton* maxNormal;
	wxButton* close;
	void CloseWindow(wxCommandEvent&) {
		GetParent()->Close();
	}
	void MinNormalWindow(wxCommandEvent&) {
		auto parent = ((wxFrame*)GetParent());
		parent->Maximize(!parent->IsMaximized());
	}
	void MinimizeWindow(wxCommandEvent&) {
		auto parent = ((wxFrame*)GetParent());
		parent->Iconize();
	}

	TopBar(wxWindow* parent)
	{
		SetBackgroundStyle(wxBG_STYLE_PAINT);
		Create(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize,
			wxBORDER_NONE | wxCLIP_CHILDREN);

		title = new wxStaticText(this, wxID_ANY, "UltimatePlayer");
		minimize = new wxButton(this, wxID_ANY, "-");
		maxNormal = new wxButton(this, wxID_ANY, "[]");
		close = new wxButton(this, wxID_ANY, "X");
		dragPanel = new wxPanel();

		minimize->Bind(wxEVT_BUTTON, &TopBar::MinimizeWindow, this);
		maxNormal->Bind(wxEVT_BUTTON, &TopBar::MinNormalWindow, this);
		close->Bind(wxEVT_BUTTON, &TopBar::CloseWindow, this);

		wxBoxSizer* sizer = new wxBoxSizer(wxHORIZONTAL);

		sizer->Add(title, 0, wxEXPAND);
		sizer->Add(dragPanel, 1, wxEXPAND);

		sizer->Add(minimize, 0, wxEXPAND);
		sizer->Add(maxNormal, 0, wxEXPAND);
		sizer->Add(close, 0, wxEXPAND);
		SetSizer(sizer);
	}
};
class OverlayPanel : public wxPanel
{
public:
	// two-stage ctor: default ctor then Create()
	OverlayPanel(wxWindow* parent)
	{
		// Check if transparency is likely to work on this platform
		wxString reason;
		if (parent && !parent->IsTransparentBackgroundSupported(&reason)) {
			wxLogWarning("Transparent background not supported: %s", reason);
			// Still continue: we'll draw a semi-opaque rect as fallback.
		}

		SetBackgroundStyle(wxBG_STYLE_PAINT);

		// Create the actual window as a child
		Create(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize,
			wxBORDER_NONE | wxCLIP_CHILDREN);

		// Optional: keep it on top of normal children in z-order
		Raise();

		// Avoid default erase background to reduce flicker
		Bind(wxEVT_ERASE_BACKGROUND, &OverlayPanel::OnEraseBackground, this);
		Bind(wxEVT_PAINT, &OverlayPanel::OnPaint, this);
		Bind(wxEVT_SIZE, &OverlayPanel::OnSize, this);

		// Initially cover entire client area
		SetSize(300, GetParent()->GetClientSize().GetHeight() - 40);
	}

private:
	void OnEraseBackground(wxEraseEvent& evt)
	{
		// Do nothing — we handle background in OnPaint (prevents flicker)
	}

	void OnSize(wxSizeEvent& evt)
	{
		// Make sure overlay matches parent client size
		SetSize(300, GetParent()->GetClientSize().GetHeight());
		Refresh();
		evt.Skip();
	}

	void OnPaint(wxPaintEvent& evt)
	{
		wxAutoBufferedPaintDC dc(this);
		dc.Clear(); // keep DC clean

		int w, h;
		GetClientSize(&w, &h);

		// right 1/3 rectangle
		int rectWidth = w / 3;
		wxRect rect(w - rectWidth, 0, rectWidth, h);

		// Use wxGraphicsContext which generally supports alpha blending across platforms
		if (wxGraphicsContext* gc = wxGraphicsContext::Create(dc)) {
			// semi-transparent black (alpha 0..255)
			wxGraphicsBrush  br = gc->CreateBrush(wxBrush(wxColour(0, 0, 0, 128)));
			gc->SetBrush(br);
			gc->SetPen(*wxTRANSPARENT_PEN);
			gc->DrawRectangle(rect.x, rect.y, rect.width, rect.height);
			delete gc;
		}
		else {
			// Fallback: try DC alpha if available (may be limited)
			dc.SetBrush(wxBrush(wxColour(0, 0, 0, 128)));
			dc.SetPen(*wxTRANSPARENT_PEN);
			dc.DrawRectangle(rect);
		}
	}
};
class VideoPanel : public wxPanel
{
public:
	VideoPanel(wxWindow* parent) : wxPanel(parent, wxID_ANY)
	{
		SetBackgroundStyle(wxBG_STYLE_PAINT);
	}
	// Helper to get native window handle pointer for libVLC
	void* GetNativeHandle()
	{
#ifdef __WXMSW__
		// On MSW, GetHWND() returns HWND (void*)
		return (void*)GetHWND();
#elif defined(__WXGTK__)
		// On GTK (X11) you'd use GDK/X11 APIs; wxWidgets provides GetHandle() as XID on some builds:
#ifdef __WXGTK__
		return (void*)GetHandle();
#else
		return nullptr;
#endif
#elif defined(__WXOSX__)
		// macOS: might need to use native Cocoa view handle (NSView*) via GetHandle()
		return (void*)GetHandle();
#else
		return nullptr;
#endif
	}
};

class MyFrame : public wxFrame
{
public:
	MyFrame()
		: wxFrame(nullptr, wxID_ANY, "wxWidgets + libVLC Player", wxDefaultPosition, wxSize(800, 600), wxFRAME_SHAPED | wxRESIZE_BORDER | wxBORDER_NONE | wxSYSTEM_MENU | wxCLIP_CHILDREN),
		vlcInstance(nullptr), vlcPlayer(nullptr)
	{
		loadPhosphorFont();
		auto* topSizer = new wxBoxSizer(wxVERTICAL);

		video = new VideoPanel(this);
		video->SetMinSize(wxSize(640, 360));
		topBar = new TopBar(this);

		topSizer->Add(topBar, 0, wxEXPAND);
		topSizer->Add(video, 1, wxEXPAND);

		auto* btnSizer = new wxBoxSizer(wxHORIZONTAL);

		btnOpen = new wxButton(this, wxID_ANY, "Open");
		btnPlay = new wxButton(this, wxID_ANY, "Play");
		btnStop = new wxButton(this, wxID_ANY, "Stop");

		btnSizer->Add(btnOpen, 0);
		btnSizer->Add(btnPlay, 0);
		btnSizer->Add(btnStop, 0);

		topSizer->Add(btnSizer, 0, wxALIGN_CENTER);

		SetSizer(topSizer);


		// Bind events
		btnOpen->Bind(wxEVT_BUTTON, &MyFrame::OnOpen, this);
		btnPlay->Bind(wxEVT_BUTTON, &MyFrame::OnPlayOrPause, this);
		btnStop->Bind(wxEVT_BUTTON, &MyFrame::OnStop, this);

		Bind(wxEVT_CLOSE_WINDOW, &MyFrame::OnClose, this);

		// init libVLC
		const char* vlc_args[] = {
			"--no-xlib", // prevents Xlib issues on some systems (harmless on windows)
		};
		vlcInstance = libvlc_new(1, vlc_args);
		if (!vlcInstance)
		{
			wxMessageBox("Failed to create libVLC instance", "Error", wxICON_ERROR);
		}
		this->SetMinSize(wxSize(640, 360));
		auto* overlay = new OverlayPanel(video);
		overlay->SetSize(wxSize(300, GetClientSize().GetHeight()));

		Bind(wxEVT_SIZE, [=](wxSizeEvent& e) {
			overlay->SetSize(300, GetClientSize().GetHeight());
			e.Skip();
			});
		Layout();

	}

	~MyFrame() override
	{
		cleanupPlayer();
		if (vlcInstance)
		{
			libvlc_release(vlcInstance);
			vlcInstance = nullptr;
		}
	}
	static wxFont phosphor;
private:
	VideoPanel* video = nullptr;
	wxButton* btnOpen = nullptr;
	wxButton* btnPlay = nullptr;
	wxButton* btnStop = nullptr;
	TopBar* topBar = nullptr;


	libvlc_instance_t* vlcInstance = nullptr;
	libvlc_media_player_t* vlcPlayer = nullptr;
	libvlc_media_t* currentMedia = nullptr;
	wxString currentPath;
	void loadPhosphorFont() {
		auto module = GetModuleHandle(NULL);
		HRSRC hRes = FindResource(module, MAKEINTRESOURCE(RT_RCDATA), MAKEINTRESOURCE(ID_PHOSPHOR));

		//HRSRC hRes = FindResourceEx(GetModuleHandleA(NULL), MAKEINTRESOURCE(ID_PHOSPHOR), RT_RCDATA,1033);
		if (!hRes)
		{
			hRes = FindResource(module, MAKEINTRESOURCE(ID_PHOSPHOR), MAKEINTRESOURCE(RT_RCDATA));
			wxMessageBox("Failed to find font resource!");

		}
		else {

			// Step 2: Load the resource into memory
			HGLOBAL hGlobal = LoadResource(NULL, hRes);
			if (!hGlobal)
			{
				wxMessageBox("Failed to load font resource!");

			}
			else {

				// Get a pointer to the font data
				LPVOID pFontData = LockResource(hGlobal);
				DWORD dwFontDataSize = SizeofResource(NULL, hRes);

				// Step 3: Add the font from memory to the Windows font table
				// A handle to the font will be automatically created
				DWORD numFonts = 0;
				AddFontMemResourceEx(pFontData, dwFontDataSize, NULL, &numFonts);
				if (numFonts == 0)
				{
					wxMessageBox("Failed to register font with the system!");

				}
			}
		}
		phosphor = wxFont(24, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL, false, "Phosphor");
	}
	void OnOpen(wxCommandEvent&)
	{
		wxFileDialog fd(this, "Open video file", wxEmptyString, wxEmptyString,
			"Media files (*.mp4;*.mkv;*.avi;*.mov)|*.mp4;*.mkv;*.avi;*.mov|All files (*.*)|*.*",
			wxFD_OPEN | wxFD_FILE_MUST_EXIST);
		if (fd.ShowModal() == wxID_OK)
		{
			currentPath = fd.GetPath();
			CreateOrSetMedia(currentPath.mb_str().data());
		}
	}

	void OnPlayOrPause(wxCommandEvent&)
	{
		if (!vlcInstance)
		{
			wxMessageBox("libVLC not initialized", "Error", wxICON_ERROR);
			return;
		}
		if (!vlcPlayer)
		{
			if (currentPath.IsEmpty())
			{
				wxMessageBox("Open a media file first.", "Info", wxICON_INFORMATION);
				return;
			}
			CreateOrSetMedia(currentPath.mb_str().data());
		}

		// set drawable / native window handle
#ifdef __WXMSW__
		void* hwnd = video->GetNativeHandle();
		if (hwnd)
		{
			libvlc_media_player_set_hwnd(vlcPlayer, hwnd);
		}
#elif defined(__WXGTK__)
		// X11: set_xwindow expects a Window (unsigned long)
		void* handle = video->GetNativeHandle();
		if (handle)
		{
			libvlc_media_player_set_xwindow(vlcPlayer, (uint32_t)(uintptr_t)handle);
		}
#elif defined(__WXOSX__)
		void* nsview = video->GetNativeHandle();
		if (nsview)
		{
			libvlc_media_player_set_nsobject(vlcPlayer, nsview);
		}
#endif
		if (libvlc_media_player_is_playing(vlcPlayer))
		{
			libvlc_media_player_pause(vlcPlayer);
			return;
		}
		else
		{
			if (libvlc_media_player_play(vlcPlayer) == -1)
			{
				wxMessageBox("Failed to play media", "Error", wxICON_ERROR);
			}
		}
	}

	void OnStop(wxCommandEvent&)
	{
		if (vlcPlayer)
		{
			libvlc_media_player_stop(vlcPlayer);
		}
	}

	void OnClose(wxCloseEvent& evt)
	{
		cleanupPlayer();
		Destroy();
	}

	void CreateOrSetMedia(const char* path)
	{
		cleanupPlayer();

		currentMedia = libvlc_media_new_path(vlcInstance, path);
		if (!currentMedia)
		{
			wxMessageBox("Failed to create libVLC media", "Error", wxICON_ERROR);
			return;
		}

		vlcPlayer = libvlc_media_player_new_from_media(currentMedia);
		// libvlc_media_release(currentMedia); // DO NOT release media until player created (we release in cleanup)
		if (!vlcPlayer)
		{
			wxMessageBox("Failed to create libVLC media player", "Error", wxICON_ERROR);
			libvlc_media_release(currentMedia);
			currentMedia = nullptr;
			return;
		}
	}

	void cleanupPlayer()
	{
		if (vlcPlayer)
		{
			libvlc_media_player_stop(vlcPlayer);
			libvlc_media_player_release(vlcPlayer);
			vlcPlayer = nullptr;
		}
		if (currentMedia)
		{
			libvlc_media_release(currentMedia);
			currentMedia = nullptr;
		}
	}
};

class MyApp : public wxApp
{
public:
	bool OnInit() override
	{
		if (!wxApp::OnInit())
			return false;
		MyFrame* frame = new MyFrame();
		frame->Show(true);
		return true;
	}
};
wxFont MyFrame::phosphor = wxFont();
wxIMPLEMENT_APP(MyApp);