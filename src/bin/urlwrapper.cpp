/*
 * Copyright 2007, Haiku. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *		François Revol, revol@free.fr
 */

/*
 * urlwrapper: wraps URL mime types around command line apps
 * or other apps that don't handle them directly.
 */
#define DEBUG 1

#include <Alert.h>
#include <Application.h>
#include <AppFileInfo.h>
#include <Debug.h>
#include <Mime.h>
#include <Message.h>
#include <TypeConstants.h>
#include <Roster.h>
#include <String.h>

#include <ctype.h>
#include <stdio.h>
#include <unistd.h>

/* compile-time configuration */
#include "urlwrapper.h"

const char *kAppSig = APP_SIGNATURE;
const char *kTrackerSig = "application/x-vnd.Be-TRAK";

#ifdef __HAIKU__
const char *kTerminalSig = "application/x-vnd.Haiku-Terminal";
#else
const char *kTerminalSig = "application/x-vnd.Be-SHEL";
#endif

#ifdef HANDLE_BESHARE
const char *kBeShareSig = "application/x-vnd.Sugoi-BeShare";
#endif

#ifdef HANDLE_IM
const char *kIMSig = "application/x-vnd.m_eiman.sample_im_client";
#endif

#ifdef HANDLE_VLC
const char *kVLCSig = "application/x-vnd.videolan-vlc";
#endif

// TODO: make a public BUrl class for use by apps ?
class Url : public BString {
public:
		Url(const char *url) : BString(url) { fStatus = ParseAndSplit(); };
		~Url() {};
status_t	InitCheck() const { return fStatus; };
status_t	ParseAndSplit();

bool		HasHost() const { return host.Length(); };
bool		HasPort() const { return port.Length(); };
bool		HasUser() const { return user.Length(); };
bool		HasPass() const { return pass.Length(); };
bool		HasPath() const { return path.Length(); };
BString		Proto() const { return BString(proto); };
BString		Full() const { return BString(full); }; // RFC1738's "sheme-part"
BString		Host() const { return BString(host); };
BString		Port() const { return BString(port); };
BString		User() const { return BString(user); };
BString		Pass() const { return BString(pass); };

BString		proto;
BString		full;
BString		host;
BString		port;
BString		user;
BString		pass;
BString		path;
private:
status_t	fStatus;
};

class UrlWrapperApp : public BApplication
{
public:
	UrlWrapperApp();
	~UrlWrapperApp();
status_t	SplitUrl(const char *url, BString &host, BString &port, BString &user, BString &pass, BString &path);
status_t	UnurlString(BString &s);
status_t	Warn(const char *url);
virtual void	RefsReceived(BMessage *msg);
virtual void	ArgvReceived(int32 argc, char **argv);
virtual void	ReadyToRun(void);
private:

};

// proto:[//]user:pass@host:port/path
status_t Url::ParseAndSplit()
{
	int32 v;
	BString left;

	v = FindFirst(":");
	if (v < 0)
		return B_BAD_VALUE;
	
	// TODO: proto and host should be lowercased.
	// see http://en.wikipedia.org/wiki/URL_normalization
	
	CopyInto(proto, 0, v);
	CopyInto(left, v + 1, Length() - v);
	// TODO: RFC1738 says the // part should indicate the uri follows the u:p@h:p/path convention, so it should be used to check for special cases.
	if (left.FindFirst("//") == 0)
		left.RemoveFirst("//");
	full = left;
	
	// path part
	// actually some apps handle file://[host]/path
	// but I have no idea what proto it implies...
	// or maybe it's just to emphasize on "localhost".
	v = left.FindFirst("/");
	if (v == 0 || proto == "file") {
		path = left;
		return 0;
	}
	// some protos actually implies path if it's the only component
	if ((v < 0) && (proto == "beshare" || proto == "irc")) { 
		path = left;
		return 0;
	}
	
	if (v > -1) {
		left.MoveInto(path, v+1, left.Length()-v);
		left.Remove(v, 1);
	}

	// user:pass@host
	v = left.FindFirst("@");
	if (v > -1) {
		left.MoveInto(user, 0, v);
		left.Remove(0, 1);
		v = user.FindFirst(":");
		if (v > -1) {
			user.MoveInto(pass, v, user.Length() - v);
			pass.Remove(0, 1);
		}
	} else if (proto == "finger") {
		// single component implies user
		// see also: http://www.subir.com/lynx/lynx_help/lynx_url_support.html
		user = left;
		return 0;
	}

	// host:port
	v = left.FindFirst(":");
	if (v > -1) {
		left.MoveInto(port, v + 1, left.Length() - v);
		left.Remove(v, 1);
	}

	// not much left...
	host = left;

	return 0;
}

status_t UrlWrapperApp::SplitUrl(const char *url, BString &host, BString &port, BString &user, BString &pass, BString &path)
{
	Url u(url);
	if (u.InitCheck() < 0)
		return u.InitCheck();
	host = u.host;
	port = u.port;
	user = u.user;
	pass = u.pass;
	path = u.path;
	return 0;
}

UrlWrapperApp::UrlWrapperApp() : BApplication(kAppSig)
{
#if 0
	BMimeType mt(B_URL_TELNET);
	if (mt.InitCheck())
		return;
	if (!mt.IsInstalled()) {
		mt.Install();
	}
#endif
#if 0
	BAppFileInfo afi;
	if (!afi.Supports(&mt)) {
		//printf("adding support for telnet url\n");
		BMessage typemsg;
		typemsg.AddString("types", url_mime);
		afi.SetSupportedTypes(&typemsg, true);
	}
#endif
}

UrlWrapperApp::~UrlWrapperApp()
{
}



status_t UrlWrapperApp::UnurlString(BString &s)
{
	// TODO: check for %00 and bail out!
	int32 length = s.Length();
	int i;
	for (i = 0; s[i] && i < length - 2; i++) {
		if (s[i] == '%' && isxdigit(s[i+1]) && isxdigit(s[i+2])) {
			int c;
			sscanf(s.String() + i + 1, "%02x", &c);
			s.Remove(i, 3);
			s.Insert((char)c, 1, i);
			length -= 2;
		}
	}
	
	return B_OK;
}

status_t UrlWrapperApp::Warn(const char *url)
{
	BString message("An application has requested the system to open the following url: \n");
	message << "\n" << url << "\n\n";
	message << "This type of urls has a potential security risk.\n";
	message << "Proceed anyway ?";
	BAlert *alert = new BAlert("Warning", message.String(), "Ok", "No", NULL, B_WIDTH_AS_USUAL, B_WARNING_ALERT);
	int32 v;
	v = alert->Go();
	if (v == 0)
		return B_OK;
	return B_ERROR;
}

void UrlWrapperApp::RefsReceived(BMessage *msg)
{
	char buff[B_PATH_NAME_LENGTH];
	int32 index = 0;
	entry_ref ref;
	char *args[] = { "urlwrapper", buff, NULL };
	status_t err;

	while (msg->FindRef("refs", index++, &ref) == B_OK) {
		BFile f(&ref, B_READ_ONLY);
		BNodeInfo ni(&f);
		BString mimetype;
		if (f.InitCheck() == B_OK && ni.InitCheck() == B_OK) {
			ni.GetType(mimetype.LockBuffer(B_MIME_TYPE_LENGTH));
			mimetype.UnlockBuffer();
#ifdef HANDLE_URL_FILES
			// http://filext.com/file-extension/URL
			if (mimetype == "wwwserver/redirection"
			 || mimetype == "application/internet-shortcut"
			 || mimetype == "application/x-url"
			 || mimetype == "message/external-body"
			 || mimetype == "text/url"
			 || mimetype == "text/x-url") {
				// http://www.cyanwerks.com/file-format-url.html
				off_t size;
				if (f.GetSize(&size) < B_OK)
					continue;
				BString contents, url;
				if (f.ReadAt(0LL, contents.LockBuffer(size), size) < B_OK)
					continue;
				while (contents.Length()) {
					BString line;
					int32 cr = contents.FindFirst('\n');
					if (cr < 0)
						cr = contents.Length();
					//contents.MoveInto(line, 0, cr);
					contents.CopyInto(line, 0, cr);
					contents.Remove(0, cr+1);
					line.RemoveAll("\r");
					if (!line.Length())
						continue;
					if (!line.ICompare("URL=", 4)) {
						line.MoveInto(url, 4, line.Length());
						break;
					}
				}
				if (url.Length()) {
					args[1] = (char *)url.String();
					//ArgvReceived(2, args);
					err = be_roster->Launch("application/x-vnd.Be.URL.http", 1, args+1);
					continue;
				}
			}
#endif
			/* eat everything as bookmark files */
#ifdef HANDLE_BOOKMARK_FILES
			if (f.ReadAttr("META:url", B_STRING_TYPE, 0LL, buff, B_PATH_NAME_LENGTH) > 0) {
				//ArgvReceived(2, args);
				err = be_roster->Launch("application/x-vnd.Be.URL.http", 1, args+1);
				continue;
			}
#endif
		}
	}
}

void UrlWrapperApp::ArgvReceived(int32 argc, char **argv)
{
#if 0
	for (int i = 1; i < argc; i++) {
		//printf("argv[%d]=%s\n", i, argv[i]);
	}
#endif
	if (argc <= 1)
		return;
	
	const char *failc = " || read -p 'Press any key'";
	const char *pausec = " ; read -p 'Press any key'";
	char *args[] = { "/bin/sh", "-c", NULL, NULL};

	Url u(argv[1]);
	BString url = u.Full();
	if (u.InitCheck() < 0) {
		fprintf(stderr, "malformed url: '%s'\n", u.String());
		return;
	}
	
	// XXX: debug
	PRINT(("PROTO='%s'\n", u.proto.String()));
	PRINT(("HOST='%s'\n", u.host.String()));
	PRINT(("PORT='%s'\n", u.port.String()));
	PRINT(("USER='%s'\n", u.user.String()));
	PRINT(("PASS='%s'\n", u.pass.String()));
	PRINT(("PATH='%s'\n", u.path.String()));
	
	if (u.proto == "telnet") {
		BString cmd("telnet ");
		if (u.HasUser())
			cmd << "-l " << u.user << " ";
		cmd << u.host;
		if (u.HasPort())
			cmd << " " << u.port;
		PRINT(("CMD='%s'\n", cmd.String()));
		cmd << failc;
		args[2] = (char *)cmd.String();
		be_roster->Launch(kTerminalSig, 3, args);
		return;
	}
	
	// see draft: http://tools.ietf.org/wg/secsh/draft-ietf-secsh-scp-sftp-ssh-uri/
	if (u.proto == "ssh") {
		BString cmd("ssh ");
		
		if (u.HasUser())
			cmd << "-l " << u.user << " ";
		if (u.HasPort())
			cmd << "-oPort=" << u.port << " ";
		cmd << u.host;
		PRINT(("CMD='%s'\n", cmd.String()));
		cmd << failc;
		args[2] = (char *)cmd.String();
		be_roster->Launch(kTerminalSig, 3, args);
		// TODO: handle errors
		return;
	}

	if (u.proto == "ftp") {
		BString cmd("ftp ");
		
		/*
		if (user.Length())
			cmd << "-l " << user << " ";
		cmd << host;
		*/
		cmd << u.full;
		PRINT(("CMD='%s'\n", cmd.String()));
		cmd << failc;
		args[2] = (char *)cmd.String();
		be_roster->Launch(kTerminalSig, 3, args);
		// TODO: handle errors
		return;
	}
	
	if (u.proto == "sftp") {
		BString cmd("sftp ");
		
		//cmd << url;
		if (u.HasPort())
			cmd << "-oPort=" << u.port << " ";
		if (u.HasUser())
			cmd << u.user << "@";
		cmd << u.host;
		if (u.HasPath())
			cmd << ":" << u.path;
		PRINT(("CMD='%s'\n", cmd.String()));
		cmd << failc;
		args[2] = (char *)cmd.String();
		be_roster->Launch(kTerminalSig, 3, args);
		// TODO: handle errors
		return;
	}

	if (u.proto == "finger") {
		BString cmd("/bin/finger ");
		
		if (u.HasUser())
			cmd << u.user;
		if (u.HasHost() == 0)
			u.host = "127.0.0.1";
		cmd << "@" << u.host;
		PRINT(("CMD='%s'\n", cmd.String()));
		cmd << pausec;
		args[2] = (char *)cmd.String();
		be_roster->Launch(kTerminalSig, 3, args);
		// TODO: handle errors
		return;
	}

#ifdef HANDLE_HTTP_WGET
	if (u.proto == "http") {
		BString cmd("/bin/wget ");
		
		//cmd << url;
		if (u.HasUser())
			cmd << u.user << "@";
		cmd << u.full;
		PRINT(("CMD='%s'\n", cmd.String()));
		cmd << pausec;
		args[2] = (char *)cmd.String();
		be_roster->Launch(kTerminalSig, 3, args);
		// TODO: handle errors
		return;
	}
#endif

#ifdef HANDLE_FILE
	if (u.proto == "file") {
		BMessage m(B_REFS_RECEIVED);
		entry_ref ref;
		UnurlString(u.path);
		if (get_ref_for_path(u.path.String(), &ref) < B_OK)
			return;
		m.AddRef("refs", &ref);
		be_roster->Launch(kTrackerSig, &m);
		return;
	}
#endif

#ifdef HANDLE_QUERY
	// XXX:TODO: split options
	if (u.proto == "query") {
		// mktemp ?
		BString qname("/tmp/query-url-temp-");
		qname << getpid() << "-" << system_time();
		BFile query(qname.String(), O_CREAT|O_EXCL);
		// XXX: should check for failure
		
		BString s;
		int32 v;
		
		UnurlString(u.full);
		// TODO: handle options (list of attrs in the column, ...)

		v = 'qybF'; // QuerY By Formula XXX: any #define for that ?
		query.WriteAttr("_trk/qryinitmode", B_INT32_TYPE, 0LL, &v, sizeof(v));
		s = "TextControl";
		query.WriteAttr("_trk/focusedView", B_STRING_TYPE, 0LL, s.String(), s.Length()+1);
		s = u.full;
		PRINT(("QUERY='%s'\n", s.String()));
		query.WriteAttr("_trk/qryinitstr", B_STRING_TYPE, 0LL, s.String(), s.Length()+1);
		query.WriteAttr("_trk/qrystr", B_STRING_TYPE, 0LL, s.String(), s.Length()+1);
		s = "application/x-vnd.Be-query";
		query.WriteAttr("BEOS:TYPE", 'MIMS', 0LL, s.String(), s.Length()+1);
		

		BEntry e(qname.String());
		entry_ref er;
		if (e.GetRef(&er) >= B_OK)
			be_roster->Launch(&er);
		return;
	}
#endif

#ifdef HANDLE_SH
	if (u.proto == "sh") {
		BString cmd(u.Full());
		if (Warn(u.String()) != B_OK)
			return;
		PRINT(("CMD='%s'\n", cmd.String()));
		cmd << pausec;
		args[2] = (char *)cmd.String();
		be_roster->Launch(kTerminalSig, 3, args);
		// TODO: handle errors
		return;
	}
#endif

#ifdef HANDLE_BESHARE
	if (u.proto == "beshare") {
		team_id team;
		BMessenger msgr(kBeShareSig);
		// if no instance is running, or we want a specific server, start it.
		if (!msgr.IsValid() || u.HasHost()) {
			be_roster->Launch(kBeShareSig, (BMessage *)NULL, &team);
			msgr = BMessenger(NULL, team);
		}
		if (u.HasHost()) {
			BMessage mserver('serv');
			mserver.AddString("server", u.host);
			msgr.SendMessage(&mserver);
			
		}
		if (u.HasPath()) {
			BMessage mquery('quer');
			mquery.AddString("query", u.path);
			msgr.SendMessage(&mquery);
		}
		// TODO: handle errors
		return;
	}
#endif

#ifdef HANDLE_IM
	if (u.proto == "icq" || u.proto == "msn") {
		// TODO
		team_id team;
		be_roster->Launch(kIMSig, (BMessage *)NULL, &team);
		BMessenger msgr(NULL, team);
		if (u.HasHost()) {
			BMessage mserver(B_REFS_RECEIVED);
			mserver.AddString("server", u.host);
			msgr.SendMessage(&httpmserver);
			
		}
		// TODO: handle errors
		return;
	}
#endif

#ifdef HANDLE_VLC
	if (u.proto == "mms" || u.proto == "rtp" || u.proto == "rtsp") {
		args[0] = "vlc";
		args[1] = (char *)u.String();
		be_roster->Launch(kVLCSig, 2, args);
		return;
	}
#endif

#ifdef HANDLE_AUDIO
	// TODO
#endif

	// vnc: ?
	// irc: ?
	// 
	// svn: ?
	// cvs: ?
	// smb: cifsmount ?
	// nfs: mount_nfs ?
	//
	// mailto: ? but BeMail & Beam both handle it already (not fully though).
	//
	// itps: pcast: podcast: s//http/ + parse xml to get url to mp3 stream...
	// audio: s//http:/ + default MediaPlayer -- see http://forums.winamp.com/showthread.php?threadid=233130
	//
	// gps: ? I should submit an RFC for that one :)

}

void UrlWrapperApp::ReadyToRun(void)
{
	Quit();
}

int main(int argc, char **argv)
{
	UrlWrapperApp app;
	if (be_app)
		app.Run();
	return 0;
}
