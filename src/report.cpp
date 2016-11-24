//
//  report.cpp
//  tattle
//
//  Created by Evan Balster on 11/12/16.
//  Copyright © 2016 imitone. All rights reserved.
//

#include <cstring>

#include "tattle.h"

#include <wx/file.h>


using namespace tattle;


bool Report::UploadURL::set(wxString url)
{
	// Must be ASCII
	if (!url.IsAscii())
	{
		std::cout << "Error: Upload URL is not ASCII." << std::endl;
		return false;
	}
	
	// Starts with http://
	{
		wxString sHTTP = wxT("http://");
		int pHTTP = url.Find(sHTTP);
		if (pHTTP != 0)
		{
			std::cout << "Error: Upload URL does not begin with 'http://'" << std::endl;
			return false;
		}
		
		pHTTP += sHTTP.length();
		url = url.Mid(pHTTP);
	}
	
	// May contain a slash after http
	int pSlash = url.Find(wxT("/"));
	if (pSlash == wxNOT_FOUND)
	{
		base = url;
		path = "";
	}
	else
	{
		base = url.Mid(0, pSlash);
		path = url.Mid(pSlash);
	}
	
	// Base must contain a dot
	if (base.Find(wxT(".")) == wxNOT_FOUND)
	{
		std::cout << "Warning: Upload URL has no dots in its name." << std::endl;
	}
	return true;
}


static void DumpString(wxMemoryBuffer &dest, const char *src)
{
	dest.AppendData((void*) src, std::strlen(src));
}

static void DumpString(wxMemoryBuffer &dest, const std::string src)
{
	dest.AppendData((void*) &src[0], src.length());
}

static void DumpString(wxMemoryBuffer &dest, const wxString &src)
{
	wxScopedCharBuffer utf8 = src.ToUTF8();
	dest.AppendData(utf8.data(), utf8.length());
}

static void DumpBuffer(wxMemoryBuffer &dest, const wxMemoryBuffer &src)
{
	dest.AppendData(src.GetData(), src.GetDataLen());
}

void Report::readFiles()
{
	for (Parameters::iterator i = params.begin(); i != params.end(); ++i)
	{
		if (i->type == PARAM_FILE_TEXT || i->type == PARAM_FILE_BIN)
		{
			// Open the file
			wxFile file(i->fname);
			
			if (!file.IsOpened())
			{
				DumpString(i->fileContents, wxT("[File not found]"));
				continue;
			}
			
			// Clear any pre-existing contents
			i->fileContents.Clear();
			
			// Read the file into the post buffer directly
			wxFileOffset fileLength = file.Length();
			
			if ((i->trimBegin || i->trimEnd) && (i->trimBegin+i->trimEnd < fileLength))
			{
				// Trim the file
				if (i->trimBegin)
				{
					size_t consumed = file.Read(i->fileContents.GetAppendBuf(i->trimBegin), i->trimBegin);
					i->fileContents.UngetAppendBuf(consumed);
					
					DumpString(i->fileContents, " ...\r\n\r\n");
				}
				if (i->trimNote.Length())
				{
					DumpString(i->fileContents, i->trimNote);
				}
				if (i->trimEnd)
				{
					DumpString(i->fileContents, "\r\n\r\n... ");
					file.Seek(-wxFileOffset(i->trimEnd), wxFromEnd);
					size_t consumed = file.Read(i->fileContents.GetAppendBuf(i->trimEnd), i->trimEnd);
					i->fileContents.UngetAppendBuf(consumed);
				}
			}
			else
			{
				size_t consumed = file.Read(i->fileContents.GetAppendBuf(fileLength), fileLength);
				i->fileContents.UngetAppendBuf(consumed);
			}
		}
	}
}
	
void Report::encodePost(wxHTTP &http) const
{
	// Boundary with 12-digit random number
	std::string boundary_id = "tattle-boundary-";
	for (unsigned i = 0; i < 12; ++i) boundary_id.push_back('0' + (std::rand()%10));
	boundary_id += "--";
	
	std::string boundary_divider = "\r\n--" + boundary_id + "\r\n";
	
	wxMemoryBuffer postBuffer;
	
	for (Parameters::const_iterator i = params.begin(); true; ++i)
	{
		// All items are preceded and succeeded by a boundary.
		DumpString(postBuffer, boundary_divider);
	
		// Not the last item, is it?
		if (i == params.end()) break;
	
		// Content disposition and name
		DumpString(postBuffer, "Content-Disposition: form-data; name=\"");
		DumpString(postBuffer, i->name);
		DumpString(postBuffer, "\"");
		if (i->fname.length())
		{
			// Filename
			DumpString(postBuffer, "; filename=\"");
			DumpString(postBuffer, i->fname);
			DumpString(postBuffer, "\"");
		}
		DumpString(postBuffer, "\r\n");
		
		// Additional content type info
		if (i->contentInfo.length())
		{
			DumpString(postBuffer, i->contentInfo);
			DumpString(postBuffer, "\r\n");
		}
		
		// Empty line...
		DumpString(postBuffer, "\r\n");
		
		// Content...
		switch (i->type)
		{
		case PARAM_STRING:
		case PARAM_FIELD:
		case PARAM_FIELD_MULTI:
			DumpString(postBuffer, i->value);
			break;
		
		case PARAM_FILE_TEXT:
		case PARAM_FILE_BIN:
			DumpBuffer(postBuffer, i->fileContents);
			break;
		case PARAM_NONE:
		default:
			DumpString(postBuffer, "Unknown Data:\r\n");
			DumpString(postBuffer, i->value);
			break;
		}
	}
	
	postBuffer.AppendByte('\0');
	cout << "HTTP Post Buffer:" << endl << ((const char*) postBuffer.GetData()) << endl;
	
	http.SetPostBuffer(wxT("multipart/form-data; boundary=\"") + wxString(boundary_id) + ("\""), postBuffer);
}
