/* SMTPProtocol - implementation of the SMTP protocol
**
** Copyright 2001 Dr. Zoidberg Enterprises. All rights reserved.
*/


#include <DataIO.h>
#include <Message.h>
#include <Alert.h>
#include <TextControl.h>
#include <Entry.h>
#include <Path.h>

#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <netdb.h>
 
#include <status.h>
#include <ProtocolConfigView.h>
#include <mail_encoding.h>
#include <MailSettings.h>
#include <ChainRunner.h>
#include <crypt.h>
#include <unistd.h>

#include "smtp.h"
#ifndef USESSL
#include "md5.h"
#endif

#include <MDRLanguage.h>

#include <sys/socket.h>
#include <sys/time.h>
#include <sys/select.h>
#include <arpa/inet.h>

#define CRLF "\r\n"
#define SMTP_RESPONSE_SIZE 8192

#ifdef DEBUG
#	define D(x) x
#	define bug printf
#else
#	define D(x) ;
#endif

/*
** Function: md5_hmac
** taken from the file rfc2104.txt
** written by Martin Schaaf <mascha@ma-scha.de>
*/
void
MD5Hmac(unsigned char *digest,
	 const unsigned char* text, int text_len,
	 const unsigned char* key, int key_len)
{
	MD5_CTX context;
	unsigned char k_ipad[64];    /* inner padding -
				      * key XORd with ipad
				      */
	unsigned char k_opad[64];    /* outer padding -
				      * key XORd with opad
				      */
	/* unsigned char tk[16]; */
	int i;

	/* start out by storing key in pads */
	memset(k_ipad, 0, sizeof k_ipad);
	memset(k_opad, 0, sizeof k_opad);
	if (key_len > 64) {
		/* if key is longer than 64 bytes reset it to key=MD5(key) */
		MD5_CTX tctx;

		MD5_Init(&tctx);
		MD5_Update(&tctx, (unsigned char*)key, key_len);
		MD5_Final(k_ipad, &tctx);
		MD5_Final(k_opad, &tctx);
	} else {
		memcpy(k_ipad, key, key_len);
		memcpy(k_opad, key, key_len);
	}

	/*
	 * the HMAC_MD5 transform looks like:
	 *
	 * MD5(K XOR opad, MD5(K XOR ipad, text))
	 *
	 * where K is an n byte key
	 * ipad is the byte 0x36 repeated 64 times
	 * opad is the byte 0x5c repeated 64 times
	 * and text is the data being protected
	 */


	/* XOR key with ipad and opad values */
	for (i = 0; i < 64; i++) {
		k_ipad[i] ^= 0x36;
		k_opad[i] ^= 0x5c;
	}

	/*
	 * perform inner MD5
	 */
	MD5_Init(&context);		      /* init context for 1st
					       * pass */
	MD5_Update(&context, k_ipad, 64);     /* start with inner pad */
	MD5_Update(&context, (unsigned char*)text, text_len); /* then text of datagram */
	MD5_Final(digest, &context);	      /* finish up 1st pass */
	/*
	 * perform outer MD5
	 */
	MD5_Init(&context);		      /* init context for 2nd
					       * pass */
	MD5_Update(&context, k_opad, 64);     /* start with outer pad */
	MD5_Update(&context, digest, 16);     /* then results of 1st
					       * hash */
	MD5_Final(digest, &context);	      /* finish up 2nd pass */
}


void
MD5HexHmac(char *hexdigest,
	     const unsigned char* text, int text_len,
	     const unsigned char* key, int key_len)
{
	unsigned char digest[16];
	int i;

	MD5Hmac(digest, text, text_len, key, key_len);
	for (i = 0; i < 16; i++)
		sprintf(hexdigest + 2 * i, "%02x", digest[i]);
}


// Authentication types recognized. Not all methods are implemented.
enum AuthType {
	LOGIN		= 1,
	PLAIN		= 1 << 2,
	CRAM_MD5	= 1 << 3,
	DIGEST_MD5	= 1 << 4
};


SMTPProtocol::SMTPProtocol(BMessage *message, BMailChainRunner *run)
	: BMailFilter(message),
	fSettings(message),
	runner(run),
	fAuthType(0)
{
	BString error_msg;
	int32 authMethod = fSettings->FindInt32("auth_method");

	if (authMethod == 2) {
		// POP3 authentification is handled here instead of SMTPProtocol::Login()
		// because some servers obviously don't like establishing the connection
		// to the SMTP server first...
		fStatus = POP3Authentification();
		if (fStatus < B_OK) {
			error_msg << MDR_DIALECT_CHOICE ("POP3 authentification failed. The server said:\n","POP3認証に失敗しました\n") << fLog;
			runner->ShowError(error_msg.String());
                        runner->Stop(true);
			return;
		}
	}

	fStatus = Open(fSettings->FindString("server"), fSettings->FindInt32("port"), authMethod == 1);
	if (fStatus < B_OK) {
		error_msg << MDR_DIALECT_CHOICE ("Error while opening connection to ","接続中にエラーが発生しました") << fSettings->FindString("server");

		if (fSettings->FindInt32("port") > 0)
			error_msg << ":" << fSettings->FindInt32("port");

		// << strerror(err) - BNetEndpoint sucks, we can't use this;
		if (fLog.Length() > 0)
			error_msg << ". The server says:\n" << fLog;
		else
			error_msg << MDR_DIALECT_CHOICE (": Connection refused or host not found.","；接続が拒否されたかサーバーが見つかりません");

		runner->ShowError(error_msg.String());
                runner->Stop(true);
		return;
	}

	const char *password = fSettings->FindString("password");
	char *passwd = get_passwd(fSettings, "cpasswd");
	if (passwd)
		password = passwd;

	fStatus = Login(fSettings->FindString("username"), password);
	delete passwd;

	if (fStatus < B_OK) {
		//-----This is a really cool kind of error message. How can we make it work for POP3?
		error_msg << MDR_DIALECT_CHOICE ("Error while logging in to ","ログイン中にエラーが発生しました\n") << fSettings->FindString("server") 
			<< MDR_DIALECT_CHOICE (". The server said:\n","サーバーエラー\n") << fLog;
		runner->ShowError(error_msg.String());
                runner->Stop(true);
	}
}


SMTPProtocol::~SMTPProtocol()
{
	Close();
}


// Check for errors?
status_t
SMTPProtocol::InitCheck(BString *verbose)
{
	if (verbose != NULL && fStatus < B_OK) {
		*verbose << MDR_DIALECT_CHOICE ("Error while fetching mail from ","受信中にエラーが発生しました") << fSettings->FindString("server")
			<< ": " << strerror(fStatus);
	}
	return fStatus;
}


// Process EMail to be sent

status_t
SMTPProtocol::ProcessMailMessage(BPositionIO **io_message, BEntry */*io_entry*/,
	BMessage *io_headers, BPath */*io_folder*/, const char */*io_uid*/)
{
	const char *from = io_headers->FindString("MAIL:from");
	const char *to = io_headers->FindString("MAIL:recipients");
	if (!to)
		to = io_headers->FindString("MAIL:to");

	if (to == NULL || from == NULL)
		fLog = "Invalid message headers";

	if (to && from && Send(to, from, *io_message) == B_OK) {
		runner->ReportProgress(0, 1);
		return B_OK;
	}

	BString error;
	MDR_DIALECT_CHOICE (
		error << "An error occurred while sending the message " << io_headers->FindString("MAIL:subject") << " to " << to << ":\n" << fLog;,
		error << io_headers->FindString("MAIL:subject") << "を" << to << "\nへ送信中にエラーが発生しました：\n" << fLog;
	)

	runner->ShowError(error.String());
	runner->ReportProgress(0, 1);
	return B_ERROR;
}


// Opens connection to server

status_t
SMTPProtocol::Open(const char *address, int port, bool esmtp)
{
	runner->ReportProgress(0, 0, MDR_DIALECT_CHOICE ("Connecting to server...","接続中..."));
        
        #ifdef USESSL
		use_ssl = (fSettings->FindInt32("flavor") == 1);
		ssl = NULL;
		ctx = NULL;
	#endif
                
        if (port <= 0)
		#ifdef USESSL
			port = use_ssl ? 465 : 25;
		#else
			port = 25;
		#endif
	
	uint32 hostIP = inet_addr(address);  // first see if we can parse it as a numeric address
	if ((hostIP == 0)||(hostIP == (uint32)-1)) {
		struct hostent * he = gethostbyname(address);
		hostIP = he ? *((uint32*)he->h_addr) : 0;
	}
   
	if (hostIP == 0)
		return EHOSTUNREACH;
		
#ifdef BONE
	_fd = socket(AF_INET, SOCK_STREAM, 0);
#else
	_fd = socket(AF_INET, 2, 0);
#endif
	if (_fd >= 0) {
		struct sockaddr_in saAddr;
		memset(&saAddr, 0, sizeof(saAddr));
		saAddr.sin_family      = AF_INET;
		saAddr.sin_port        = htons(port);
		saAddr.sin_addr.s_addr = hostIP;
		int result = connect(_fd, (struct sockaddr *) &saAddr, sizeof(saAddr));
		if (result < 0) {
#ifdef BONE
			close(_fd);
#else
			closesocket(_fd);
#endif
			_fd = -1;
			return errno;
		}
	} else {
		return errno;
	}

#ifdef USESSL
	if (use_ssl) {
		SSL_library_init();
    	SSL_load_error_strings();
    	RAND_seed(this,sizeof(SMTPProtocol));
    	/*--- Because we're an add-on loaded at an unpredictable time, all
    	      the memory addresses and things contained in ourself are
    	      esssentially random. */
    	
    	ctx = SSL_CTX_new(SSLv23_method());
    	ssl = SSL_new(ctx);
    	sbio=BIO_new_socket(_fd,BIO_NOCLOSE);
    	SSL_set_bio(ssl,sbio,sbio);
    	
    	if (SSL_connect(ssl) <= 0) {
    		BString error;
			error << "Could not connect to SMTP server " << fSettings->FindString("server");
			if (port != 465)
				error << ":" << port;
			error << ". (SSL Connection Error)";
			runner->ShowError(error.String());
			SSL_CTX_free(ctx);
			#ifdef BONE
				close(_fd);
			#else
				closesocket(_fd);
			#endif
                        _fd = -1;
			runner->Stop(true);
			return B_OK;
		}
	}
	
    #endif

	BString line;
	ReceiveResponse(line);

	char *cmd = new char[::strlen(address)+8];
	if (!esmtp)
		::sprintf(cmd,"HELO %s"CRLF, address);
	else
		::sprintf(cmd,"EHLO %s"CRLF, address);

	if (SendCommand(cmd) != B_OK) {
		delete[] cmd;
		return B_ERROR;
	}

	delete[] cmd;

	// Check auth type
	if (esmtp) {
		const char *res = fLog.String();
		char *p;
		if ((p = ::strstr(res, "250-AUTH")) != NULL) {
			if(::strstr(p, "LOGIN"))
				fAuthType |= LOGIN;
			if(::strstr(p, "PLAIN"))
				fAuthType |= PLAIN;
			if(::strstr(p, "CRAM-MD5"))
				fAuthType |= CRAM_MD5;
			if(::strstr(p, "DIGEST-MD5"))
				fAuthType |= DIGEST_MD5;
		}
	}
	return B_OK;
}


status_t
SMTPProtocol::POP3Authentification()
{
	// find the POP3 filter of the other chain - identify by name...
	BList chains;
	if (GetInboundMailChains(&chains) < B_OK) {
		fLog = "Cannot get inbound chains";
		return B_ERROR;
	}

	BMailChainRunner *parent = runner;
	BMailChain *chain = NULL;
	for (int i = chains.CountItems(); i-- > 0;)
	{
		chain = (BMailChain *)chains.ItemAt(i);
		if (chain != NULL && !strcmp(chain->Name(), parent->Chain()->Name()))
			break;
		chain = NULL;
	}
	if (chain != NULL)
	{
		// found mail chain! let's check for the POP3 protocol
		BMessage msg;
		entry_ref ref;
		if (chain->GetFilter(0, &msg, &ref) >= B_OK)
		{
			BPath path(&ref);
			if (path.InitCheck() >= B_OK && !strcmp(path.Leaf(), "POP3"))
			{
				// protocol matches, go execute it!

				image_id image = load_add_on(path.Path());

				fLog = "Cannot load POP3 add-on";
				if (image >= B_OK)
				{
					BMailFilter *(* instantiate)(BMessage *, BMailChainRunner *);
					status_t status = get_image_symbol(image, "instantiate_mailfilter",
						B_SYMBOL_TYPE_TEXT, (void **)&instantiate);
					if (status >= B_OK)
					{
						msg.AddInt32("chain", chain->ID());
						msg.AddBool("login_and_do_nothing_else_of_any_importance",true);

						// instantiating and deleting should be enough
						BMailFilter *filter = (*instantiate)(&msg, runner);
						delete filter;
					}
					else
						fLog = "Cannot run POP3 add-on, symbol not found";

					unload_add_on(image);
					return status;
				}
			}
		}
		else
			fLog = "Could not get inbound protocol";
	}
	else
		fLog = "Cannot find inbound chain";

	for (int i = chains.CountItems(); i-- > 0;)
	{
		chain = (BMailChain *)chains.ItemAt(i);
		delete chain;
	}

	return B_ERROR;
}


status_t
SMTPProtocol::Login(const char *_login, const char *password)
{
	if (fAuthType == 0)
		return B_OK;

	const char *login = _login;
	char hex_digest[33];
	BString out;

	int32 loginlen = ::strlen(login);
	int32 passlen = ::strlen(password);

	if (fAuthType & CRAM_MD5) {
		//******* CRAM-MD5 Authentication ( not tested yet.)
		SendCommand("AUTH CRAM-MD5"CRLF);
		const char *res = fLog.String();

		if (strncmp(res, "334", 3) != 0)
			return B_ERROR;
		char *base = new char[::strlen(&res[4])+1];
		int32 baselen = ::strlen(base);
		baselen = ::decode_base64(base, base, baselen);
		base[baselen] = '\0';

		D(bug("base: %s\n", base));

		::MD5HexHmac(hex_digest, (const unsigned char *)base, (int)baselen,
			(const unsigned char *)password, (int)passlen);

		D(bug("%s\n%s\n", base, hex_digest));

		delete[] base;

		BString preResponse, postResponse;
		preResponse = login;
		preResponse << " " << hex_digest << CRLF;
		char *resp = postResponse.LockBuffer(preResponse.Length() * 2 + 10);
		baselen = ::encode_base64(resp, preResponse.String(), preResponse.Length(), true /* headerMode */);
		resp[baselen] = 0;
		postResponse.UnlockBuffer();
		postResponse.Append(CRLF);

		SendCommand(postResponse.String());

		res = fLog.String();
		if (atol(res) < 500)
			return B_OK;
	}
	if (fAuthType & DIGEST_MD5) {
		//******* DIGEST-MD5 Authentication ( not written yet..)
		fLog = "DIGEST-MD5 Authentication is not supported";
	}
	if (fAuthType & LOGIN) {
		//******* LOGIN Authentication ( tested. works fine)
		ssize_t encodedsize; // required by our base64 implementation

		SendCommand("AUTH LOGIN"CRLF);
		const char *res = fLog.String();

		if (strncmp(res, "334", 3) != 0)
			return B_ERROR;

		// Send login name as base64
		char *login64 = new char[loginlen*3 + 6];
		encodedsize = ::encode_base64(login64, (char *)login, loginlen, true /* headerMode */);
		login64[encodedsize] = 0;
		strcat (login64, CRLF);
		SendCommand(login64);
		delete[] login64;

		res = fLog.String();
		if (strncmp(res,"334",3) != 0)
			return B_ERROR;

		// Send password as base64
		login64 = new char[passlen*3 + 6];
		encodedsize = ::encode_base64(login64, (char *)password, passlen, true /* headerMode */);
		login64[encodedsize] = 0;
		strcat (login64, CRLF);
		SendCommand(login64);
		delete[] login64;

		res = fLog.String();
		if (atol(res) < 500)
			return B_OK;
	}
	if (fAuthType & PLAIN) {
		//******* PLAIN Authentication ( not tested yet.)
		BString preResponse, postResponse;
		char *stringPntr;
		ssize_t encodedLength;
		stringPntr = preResponse.LockBuffer(loginlen * 2 + passlen + 3);
		sprintf (stringPntr, "%s%c%s%c%s", login, 0, login, 0, password);
		preResponse.UnlockBuffer(loginlen * 2 + passlen + 3);
		stringPntr = postResponse.LockBuffer(preResponse.Length() * 3);
		encodedLength = ::encode_base64(stringPntr, preResponse.String(),
			preResponse.Length(), true /* headerMode */);
		stringPntr[encodedLength] = 0;
		postResponse.UnlockBuffer();
		postResponse.Prepend("AUTH PLAIN ");
		postResponse << CRLF;

		SendCommand(postResponse.String());

		const char *res = fLog.String();
		if (atol(res) < 500)
			return B_OK;
	}
	return B_ERROR;
}


void
SMTPProtocol::Close()
{

	BString cmd = "QUIT";
	cmd += CRLF;

	if (SendCommand(cmd.String()) != B_OK) {
		// Error
	}

#ifdef USESSL
        if (use_ssl)  {
                if (ssl)
                        SSL_shutdown(ssl);
                if (ctx)
                        SSL_CTX_free(ctx);
        }
#endif

#ifdef BONE
	close(_fd);
#else
	closesocket(_fd);
#endif
}


/** Send mail */

status_t
SMTPProtocol::Send(const char *to, const char *from, BPositionIO *message)
{
	BString cmd = from;
	cmd.Remove(0, cmd.FindFirst("\" <") + 2);
	cmd.Prepend("MAIL FROM: ");
	cmd += CRLF;
	if (SendCommand(cmd.String()) != B_OK)
		return B_ERROR;

	int32 len = strlen(to);
	BString addr("");
	for (int32 i = 0;i < len;i++) {
		char c = to[i];
		if (c != ',')
			addr += (char)c;
		if (c == ','||i == len-1) {
			if(addr.Length() == 0)
				continue;
			cmd = "RCPT TO: ";
			cmd << addr.String() << CRLF;
			if (SendCommand(cmd.String()) != B_OK)
				return B_ERROR;

			addr ="";
		}
	}

	cmd = "DATA";
	cmd += CRLF;
	if (SendCommand(cmd.String()) != B_OK)
		return B_ERROR;

	// Send the message data.  Convert lines starting with a period to start
	// with two periods and so on.  The actual sequence is CR LF Period.  The
	// SMTP server will remove the periods.  Of course, the POP server may then
	// add some of its own, but the POP client should take care of them.

	ssize_t		amountRead;
	ssize_t		amountToRead;
	ssize_t		amountUnread;
	ssize_t		bufferLen = 0;
	const int	bufferMax = 2000;
	bool		foundCRLFPeriod;
	int			i;
	bool		messageEndedWithCRLF = false;

	message->Seek(0, SEEK_END);
	amountUnread = message->Position();
	message->Seek(0, SEEK_SET);
	char *data = new char[bufferMax];

	while (true) {
		// Fill the buffer if it is getting low, but not every time, to avoid
		// small reads.
		if (bufferLen < bufferMax / 2) {
			amountToRead = bufferMax - bufferLen;
			if (amountToRead > amountUnread)
				amountToRead = amountUnread;
			if (amountToRead > 0) {
				amountRead = message->Read (data + bufferLen, amountToRead);
				if (amountRead <= 0 || amountRead > amountToRead)
					amountUnread = 0; // Just stop reading when an error happens.
				else {
					amountUnread -= amountRead;
					bufferLen += amountRead;
				}
			}
		}

		// Look for the next CRLFPeriod triple.
		foundCRLFPeriod = false;
		for (i = 0; i <= bufferLen - 3; i++) {
			if (data[i] == '\r' && data[i+1] == '\n' && data[i+2] == '.') {
				foundCRLFPeriod = true;
				// Send data up to the CRLF, and include the period too.
                        #ifdef USESSL
                                if (use_ssl) {
                                        if (SSL_write(ssl,data,i + 3) < 0) {
                                            amountUnread = 0; // Stop when an error happens.
                                            bufferLen = 0;
                                            break;
                                        }
                                } else
                        #endif
				if (send (_fd,data, i + 3,0) < 0) {
					amountUnread = 0; // Stop when an error happens.
					bufferLen = 0;
					break;
				}
				runner->ReportProgress (i + 2 /* Don't include the double period here */,0);
				// Move the data over in the buffer, but leave the period there
				// so it gets sent a second time.
				memmove(data, data + (i + 2), bufferLen - (i + 2));
				bufferLen -= i + 2;
				break;
			}
		}

		if (!foundCRLFPeriod) {
			if (amountUnread <= 0) { // No more data, all we have is in the buffer.
				if (bufferLen > 0) {
                            #ifdef USESSL
                                    if (use_ssl)
                                            SSL_write(ssl,data,bufferLen);
                                    else
                            #endif
					send (_fd,data, bufferLen,0);
					runner->ReportProgress (bufferLen,0);
					if (bufferLen >= 2)
						messageEndedWithCRLF = (data[bufferLen-2] == '\r' &&
							data[bufferLen-1] == '\n');
				}
				break; // Finished!
			}

			// Send most of the buffer, except a few characters to overlap with
			// the next read, in case the CRLFPeriod is split between reads.
			if (bufferLen > 3) {
                        #ifdef USESSL
                            if (use_ssl) {
                                    if (SSL_write(ssl,data,bufferLen - 3) < 0)
                                        break;
                            } else
                        #endif
				if (send (_fd,data, bufferLen - 3,0) < 0)
					break; // Stop when an error happens.
				runner->ReportProgress (bufferLen - 3,0);
				memmove (data, data + bufferLen - 3, 3);
				bufferLen = 3;
			}
		}
	}
	delete [] data;

	if (messageEndedWithCRLF)
		cmd = "."CRLF; // The standard says don't add extra CRLF.
	else
		cmd = CRLF"."CRLF;

	if (SendCommand(cmd.String()) != B_OK)
		return B_ERROR;

	return B_OK;
}


// Receives response from server.

int32
SMTPProtocol::ReceiveResponse(BString &out)
{
	out = "";
	int32 len = 0,r;
	char buf[SMTP_RESPONSE_SIZE];
	bigtime_t timeout = 1000000*180; // timeout 180 secs
	
	struct timeval tv;
	struct fd_set fds; 

	tv.tv_sec = long(timeout / 1e6); 
	tv.tv_usec = long(timeout-(tv.tv_sec * 1e6)); 
	
	/* Initialize (clear) the socket mask. */ 
	FD_ZERO(&fds);
	
	/* Set the socket in the mask. */ 
	FD_SET(_fd, &fds); 
        int result = -1;
#ifdef USESSL
        if ((use_ssl) && (SSL_pending(ssl)))
            result = 1;
        else
#endif
            result = select(32, &fds, NULL, NULL, &tv);
	if (result < 0)
		return errno;
	
	if (result > 0) {
		while (1) {
                 #ifdef USESSL
			if (use_ssl)
				r = SSL_read(ssl,buf,SMTP_RESPONSE_SIZE - 1);
			else
		  #endif
			r = recv(_fd,buf, SMTP_RESPONSE_SIZE - 1,0);
			if (r <= 0)
				break;

			len += r;
			out.Append(buf, r);
			if (strstr(buf, CRLF))
				break;
		}
	} else
		fLog = "SMTP socket timeout.";

	D(bug("S:%s\n", out.String()));
	return len;
}


// Sends SMTP command. Result kept in fLog

status_t
SMTPProtocol::SendCommand(const char *cmd)
{
	D(bug("C:%s\n", cmd));

#ifdef USESSL
	if (use_ssl) {
		if (SSL_write(ssl,cmd,::strlen(cmd)) < 0)
                    return B_ERROR;
	} else
#endif
	if (send(_fd,cmd, ::strlen(cmd),0) < 0)
		return B_ERROR;

	fLog = "";

	// Receive
	while (1) {
		int32 len = ReceiveResponse(fLog);

		if (len <= 0) {
			D(bug("SMTP: len == %ld\n", len));
			return B_ERROR;
		}

		if (fLog.Length() > 4 && (fLog[3] == ' ' || fLog[3] == '-'))
		{
			int32 num = atol(fLog.String());
			D(bug("ReplyNumber: %ld\n", num));

			if (num >= 500)
				return B_ERROR;

			break;
		}
	}

	return B_OK;
}


// Instantiate hook
BMailFilter *
instantiate_mailfilter(BMessage *settings, BMailChainRunner *status)
{
	return new SMTPProtocol(settings, status);
}


// Configuration interface
BView *
instantiate_config_panel(BMessage *settings, BMessage *)
{
	BMailProtocolConfigView *view = new BMailProtocolConfigView(B_MAIL_PROTOCOL_HAS_AUTH_METHODS | B_MAIL_PROTOCOL_HAS_USERNAME | B_MAIL_PROTOCOL_HAS_PASSWORD | B_MAIL_PROTOCOL_HAS_HOSTNAME
       
         #ifdef USESSL
            | B_MAIL_PROTOCOL_HAS_FLAVORS);
        view->AddFlavor("Unencrypted");
        view->AddFlavor("SSL");
	#else
            );
	#endif

	view->AddAuthMethod(MDR_DIALECT_CHOICE ("None","無し"), false);
	view->AddAuthMethod(MDR_DIALECT_CHOICE ("ESMTP","ESMTP"));
	view->AddAuthMethod(MDR_DIALECT_CHOICE ("POP3 before SMTP","送信前に受信する"), false);

	BTextControl *control = (BTextControl *)(view->FindView("host"));
	control->SetLabel(MDR_DIALECT_CHOICE ("SMTP Host: ","SMTPサーバ: "));
	//control->SetDivider(be_plain_font->StringWidth("SMTP Host: "));

	view->SetTo(settings);

	return view;
}
