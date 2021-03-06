//
//  report_http.cpp
//  tattle
//
//  Created by Evan Balster on 11/24/16.
//

#include <iostream>
#include <algorithm>

#include "tattle.h"

#include <wx/progdlg.h>
#include <wx/sstream.h>
#include <wx/uri.h>
#include <wx/frame.h>


using namespace tattle;


bool Report::ParsedURL::set(wxString url)
{
	wxURI uri(url);
	
	bool ok = true;
	
	if (uri.HasScheme() && uri.GetScheme() != "http")
	{
		std::cout << "Error: URL is not HTTP." << std::endl;
		ok = false;
	}
	
	if (!uri.HasServer())
	{
		std::cout << "Error: URL does not specify a host." << std::endl;
		ok = false;
	}
	
	host = uri.GetServer();
	path = uri.GetPath();
	port = 80;
	
	// Optionally parse a port
	if (uri.HasPort())
	{
		if (!uri.GetPort().ToULong(&port))
		{
			std::cout << "Error: invalid port `" << uri.GetPort() << "'" << std::endl;
			ok = false;
		}
	}
	
	if (uri.HasFragment()) std::cout << "Warning: ignoring URL fragment: " << uri.GetFragment() << std::endl;
	if (uri.HasUserInfo()) std::cout << "Warning: ignoring URL userinfo: " << uri.GetUserInfo() << std::endl;
	if (uri.HasQuery()   ) std::cout << "Warning: ignoring URL query: "    << uri.GetQuery()    << std::endl;

	return ok;
}


Report::Reply::Reply() :
	connected(false), statusCode(0), error(wxPROTO_NOERR), command(SC_NONE), icon("")
{
}

bool Report::Reply::ok()       const
{
	return connected && error == wxPROTO_NOERR;
}
bool Report::Reply::valid()    const
{
	return ok() && (message.Length() || title.Length() || link.Length() || command != SC_NONE);
}
bool Report::Reply::sentLink() const
{
	return ok() && link.Length();
}

wxString tattle::GetTagContents(const wxString &reply, const wxString &tagName)
{
	wxString
		tagOpen  = wxT("<") +tagName+wxT(">"),
		tagClose = wxT("</")+tagName+wxT(">");
	
	int
		ptOpen = reply.Find(tagOpen),
		ptClose = reply.Find(tagClose);
	
	if (ptOpen != wxNOT_FOUND && ptClose != wxNOT_FOUND)
	{
		ptOpen += tagOpen.Length();
		if (ptOpen <= ptClose)
			return reply.Mid(ptOpen, ptClose-ptOpen);
	}
	
	return wxString();
}

void Report::Reply::parseRaw(const ParsedURL &url)
{
	wxString comm, iconName;
	
	title    = GetTagContents(raw, wxT("tattle-title")),
	message  = GetTagContents(raw, wxT("tattle-message")),
	link     = GetTagContents(raw, wxT("tattle-link"));
	comm     = GetTagContents(raw, wxT("tattle-command"));
	iconName = GetTagContents(raw, wxT("tattle-icon"));

	icon = uiConfig.GetIconID(iconName);
	
	if      (comm == wxT("STOP"))         command = SC_STOP;
	else if (comm == wxT("PROMPT"))       command = SC_PROMPT;
	else if (comm == wxT("STOP-ON-LINK")) command = SC_STOP_ON_LINK;
	else                                  command = SC_NONE;
	
	if (link.Length())
	{
		// Format into an HTTP link based at the upload URL.
		//    Don't allow links to other websites.
		link = wxT("http://") + url.host + "/" + link;
	}
}

void Report::Reply::connect(wxHTTP &http, const ParsedURL &url)
{
	connected = http.Connect(url.host, (unsigned short) url.port);

	error      = http.GetError();
	if (error != wxPROTO_NOERR) connected = false;

	if (connected)
	{
		// Hooway
	}
	else
	{
		std::cout << "Tattle: failed connection to " << url.host << " (error " << error << ")" << std::endl;
		statusCode = 0;
	}
}

void Report::Reply::pull(wxHTTP &http, const ParsedURL &url, wxString query)
{
	if (connected)
	{
		std::cout << "Tattle: connected to " << url.host << std::endl;
	
		wxString path = url.path;
		if (!path.Length()) path = wxT("/");
		
		if (query.Length() && query[0] != wxT('?')) query = wxT("?")+query;

		wxInputStream *httpStream = nullptr;
		error      = http.GetError();
		if (error != wxPROTO_NOERR)
		{
			std::cout << ", status " << statusCode << ", error " << error;
		}
		else
		{
			// Consume reply and/or error code from server
			httpStream = http.GetInputStream(path+query);
		}
		
		raw = wxT("");
		if (httpStream)
		{
			wxStringOutputStream out_stream(&raw);
			httpStream->Read(out_stream);
		}
		
		statusCode = http.GetResponse();
		error      = http.GetError();
		
		bool success = (error == wxPROTO_NOERR);
		
		std::cout << "Tattle: query `" << (path+query) << ": " <<
			(success ? "success" : "failure");
		if (success)
		{
			std::cout << "... reply:" << std::endl << raw;
		}
		else
		{
			std::cout << ", status " << statusCode << ", error " << error;
		}
		std::cout << std::endl;
		
		if (httpStream) wxDELETE(httpStream);
	}
	else
	{
		std::cout << "Tattle: failed connection to " << url.host << std::endl;
	
		// Failed to connect...
		raw = wxT("");
		
		statusCode = 0;
		error      = http.GetError();
	}
	
	// Parse raw reply string into bits
	parseRaw(url);
}

void Report::httpAction(wxHTTP &http, const ParsedURL &url, Reply &reply, wxProgressDialog *prog, bool isQuery) const
{
	if (prog)
	{
		// Set icon and show
		prog->SetIcon(wxArtProvider::GetIcon(uiConfig.defaultIcon));
		prog->Show();
		prog->Raise();
	}

	encodePost(http, isQuery);

	do
	{
		// Connect to server
		if (prog)
		{
			prog->Update(10, "Connecting to " + url.host + "...");
			wxYield();
		}
		reply.connect(http, url);

		//wxSleep(1);  For UI testing

		if (!reply.connected) break;

		// Post and download reply
		if (uiConfig.showProgress)
		{
			if (isQuery)
				prog->Update(30, "Talking with " + url.host + "...");
			else
				prog->Update(25, "Sending to " + url.host + "...\nThis may take a while.");
			wxYield();
		}
		reply.pull(http, url);

		//wxSleep(1);  For UI testing

		if (uiConfig.showProgress) prog->Update(60);
	}
	while (false);

	http.Close();
}

Report::Reply Report::httpQuery(wxWindow *parent) const
{
	//wxString query = preQueryString();
	
	if (uiConfig.showProgress && parent && uiConfig.stayOnTop)
	{
		// Hack for ordering issue
		parent->Hide();
		parent = NULL;
	}

	Reply reply;
	wxHTTP http; http.SetTimeout(6);

	if (uiConfig.showProgress)
	{
		wxProgressDialog dialog("Looking for solutions...", "Preparing...", 60, parent,
			wxPD_APP_MODAL | wxPD_AUTO_HIDE | uiConfig.style());

		httpAction(http, queryURL, reply, &dialog, true);
	}
	else
	{
		httpAction(http, queryURL, reply, NULL, true);
	}

	// Ordering issue hack
	if (uiConfig.showProgress && parent && uiConfig.stayOnTop) parent->Show();
	
	return reply;
}

Report::Reply Report::httpPost(wxWindow *parent) const
{
	if (uiConfig.showProgress && parent && uiConfig.stayOnTop)
	{
		// Hack for ordering issue on Mac
		parent->Hide();
		parent = NULL;
	}

	Reply reply;
	wxHTTP http; http.SetTimeout(60);

	if (uiConfig.showProgress)
	{
		wxProgressDialog dialog("Sending...", "Preparing Report...", 60, parent,
			wxPD_APP_MODAL | wxPD_AUTO_HIDE | uiConfig.style());

		httpAction(http, postURL, reply, &dialog, false);
	}
	else
	{
		httpAction(http, postURL, reply, NULL, false);
	}
	
	
	return reply;
}

bool Report::httpTest(const ParsedURL &url) const
{
	wxHTTP http; http.SetTimeout(5);
	
	bool connected = http.Connect(url.host, url.port);
	
	http.Close();
	
	return connected;
}
