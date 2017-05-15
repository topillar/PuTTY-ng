#include "online_session_manager.h"
#include "putty.h"
#include "../adb/AdbManager.h"
#include <Windows.h>
#include <Winhttp.h>
#include "atlconv.h" 
extern "C"{
#include <curl/curl.h>
}
#include "../fsm/SocketConnection.h"
using namespace Net;

#include <algorithm>
#include <iostream>
#include <string>
#include "../base/rand_util.h"
#undef min
#include "../base/algorithm/base64/base64.h"

#include <shellapi.h>
struct ssh_hash {
	void *(*init)(void); /* also allocates context */
	void *(*copy)(const void *);
	void(*bytes)(void *, const void *, int);
	void(*final)(void *, unsigned char *); /* also frees context */
	void(*free)(void *);
	int hlen; /* output length in bytes */
	const char *text_name;
};

const char* const client_id = "924120620403-5qdo3gbs7eleo9a2v0ug039vsk758vln.apps.googleusercontent.com";

OnlineSessionManager::OnlineSessionManager()
	: mTcpServer(this)
{
	mHandlingIndex = 0;
}

OnlineSessionManager::~OnlineSessionManager()
{

}

void upload_sessions()
{
	g_online_session_manager->upload_sessions();
}
int OnlineSessionManager::upload_sessions()
{
	//if (!mWaitingList.empty()){ return -1; }
	//
	//struct sesslist sesslist;
	//get_sesslist(&sesslist, TRUE);
	//for (int i = 0; i < sesslist.nsessions; i++) {
	//	if (strcmp(sesslist.sessions[i], DEFAULT_SESSION_NAME) == 0){ continue; }
	//	if (strcmp(sesslist.sessions[i], ANDROID_DIR_FOLDER_NAME) == 0){ continue; }
	//	mWaitingList.push_back(sesslist.sessions[i]);
	//}
	//get_sesslist(&sesslist, FALSE);
	//
	//mHandlingIndex = 0;
	//if (!mWaitingList.empty()){ handle_waiting_list(); }
	handle_waiting_list();
	return 0;
}

void OnlineSessionManager::handle_waiting_list()
{
	//if (mWaitingList.empty()) return ;
	//if (mHandlingIndex < 0 || mHandlingIndex >= mWaitingList.size()) return;
	//string& session_name = mWaitingList[mHandlingIndex];
	//
	//char tmp_file_path[512] = { 0 };
	//GetTempPathA(sizeof(tmp_file_path)-1, tmp_file_path);
	//snprintf(tmp_file_path, sizeof(tmp_file_path)-1, "%s\\%s", tmp_file_path, "upload.sess.tmp");
	//backup_settings(session_name.c_str(), tmp_file_path);
	//g_adbm_processor->process(0, NEW_PROCESSOR_JOB(OnlineSessionManager::upload_file, tmp_file_path));

	g_adbm_processor->process(0, NEW_PROCESSOR_JOB(OnlineSessionManager::upload_file, ""));
}

static size_t query_auth_write_cb(void *_ptr, size_t _size, size_t _nmemb, void *_data)
{
	size_t realsize = _size * _nmemb;
	string* response_str = (string*)_data;
	response_str->append((char*)_ptr, realsize);
	return realsize;
}

void OnlineSessionManager::handleInput(SocketConnectionPtr connection)
{
	char buffer[1024];
	unsigned len = 1;
	bool canWrite = true;
	connection->resetHeartbeatTimeoutCounter();
	while (len > 0 && (canWrite = connection->isWBufferHealthy()))
	{
		len = connection->getInput(buffer, sizeof(buffer));
		mRsp.append(buffer, len);
	}
}

void OnlineSessionManager::handleClose(SocketConnectionPtr theConnection)
{

}

//-----------------------------------------------------------------------------

bool getProxyAddr(const string& strAddr, char* strDestAddr, const char* type)
{
	int nStart = strAddr.find(type);
	if (nStart != -1)
	{
		nStart += strlen(type);
		int nLen = strAddr.find(';', nStart + 1) - nStart;
		strcpy(strDestAddr, strAddr.substr(nStart, nLen).c_str());
		return true;
	}
	return false;
}

int getIEProxy(const char* host, int& proxytype, char* strAddr, bool bUserHttps)
{
	USES_CONVERSION;
	bool fAutoProxy = false;
	WINHTTP_PROXY_INFO autoProxyInfo = { 0 };

	WINHTTP_AUTOPROXY_OPTIONS autoProxyOptions = { 0 };
	WINHTTP_CURRENT_USER_IE_PROXY_CONFIG ieProxyConfig = { 0 };
	if (WinHttpGetIEProxyConfigForCurrentUser(&ieProxyConfig))
	{
		if (ieProxyConfig.fAutoDetect)
		{
			fAutoProxy = true;
		}

		if (ieProxyConfig.lpszAutoConfigUrl != NULL)
		{
			fAutoProxy = true;
			autoProxyOptions.lpszAutoConfigUrl = ieProxyConfig.lpszAutoConfigUrl;
		}
	}
	else
	{
		// use autoproxy
		fAutoProxy = true;
	}

	if (fAutoProxy)
	{
		if (autoProxyOptions.lpszAutoConfigUrl != NULL)
		{
			autoProxyOptions.dwFlags = WINHTTP_AUTOPROXY_CONFIG_URL;
		}
		else
		{
			autoProxyOptions.dwFlags = WINHTTP_AUTOPROXY_AUTO_DETECT;
			autoProxyOptions.dwAutoDetectFlags = WINHTTP_AUTO_DETECT_TYPE_DHCP | WINHTTP_AUTO_DETECT_TYPE_DNS_A;
		}

		// basic flags you almost always want
		autoProxyOptions.fAutoLogonIfChallenged = TRUE;

		HINTERNET session = ::WinHttpOpen(0, // no agent string
			WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
			WINHTTP_NO_PROXY_NAME,
			WINHTTP_NO_PROXY_BYPASS,
			WINHTTP_FLAG_ASYNC);

		// here we reset fAutoProxy in case an auto-proxy isn't actually
		// configured for this url

		fAutoProxy = WinHttpGetProxyForUrl(session, A2W(host), &autoProxyOptions, &autoProxyInfo);
		if (session) WinHttpCloseHandle(session);
	}

	if (fAutoProxy)
	{
		// set proxy options for libcurl based on autoProxyInfo
		//autoProxyInfo.lpszProxy
		//curl_easy_setopt(curl,CURLOPT_PROXY,autoProxyInfo.lpszProxy);
		if (autoProxyInfo.lpszProxy)
		{
			strncpy(strAddr, W2A(autoProxyInfo.lpszProxy), 256);
			proxytype = CURLPROXY_HTTP;
		}
		else
		{
			return -1;
		}
	}
	else
	{
		if (ieProxyConfig.lpszProxy != NULL)
		{
			// IE has an explicit proxy. set proxy options for libcurl here
			// based on ieProxyConfig
			//
			// note that sometimes IE gives just a single or double colon
			// for proxy or bypass list, which means "no proxy"
			strncpy(strAddr, W2A(ieProxyConfig.lpszProxy), 256);
			proxytype = CURLPROXY_HTTP;

			///may be like this: "http=127.0.0.1:8888;https=127.0.0.1:8888;ftp=127.0.0.1:8888;socks=127.0.0.1:8888" "127.0.0.1:8888"
			string strProxyAddr(strAddr);
			if (strProxyAddr.find('=') != -1)
			{
				bool bFind = false;
				if (bUserHttps && getProxyAddr(strProxyAddr, strAddr, "https=")) bFind = true;
				if (bFind == false && getProxyAddr(strProxyAddr, strAddr, "http=")) bFind = true;
				if (bFind == false && getProxyAddr(strProxyAddr, strAddr, "socks="))
				{
					proxytype = CURLPROXY_SOCKS5;
				}
			}
		}
		else
		{
			proxytype = -1;
			// there is no auto proxy and no manually configured proxy
		}
	}

	if (autoProxyInfo.lpszProxy != NULL) GlobalFree(autoProxyInfo.lpszProxy);
	if (autoProxyInfo.lpszProxyBypass != NULL) GlobalFree(autoProxyInfo.lpszProxyBypass);
	//if(autoProxyOptions.lpszAutoConfigUrl != NULL) GlobalFree(autoProxyOptions.lpszAutoConfigUrl);
	if (ieProxyConfig.lpszAutoConfigUrl != NULL) GlobalFree(ieProxyConfig.lpszAutoConfigUrl);
	if (ieProxyConfig.lpszProxy != NULL) GlobalFree(ieProxyConfig.lpszProxy);
	if (ieProxyConfig.lpszProxyBypass != NULL) GlobalFree(ieProxyConfig.lpszProxyBypass);

	return proxytype;
}

static void base64urlencodeNoPadding(string& input)
{
	for (int i = 0; i < input.length(); i++)
	{
		if (input[i] == '+' || input[i] == '/'){ input[i] = '-'; }
	}

	for (int i = input.length() - 1; i >= 0; i--)
	{
		if (input[i] == '='){ input.erase(i); }
	}
}

static string sha256(std::string input)
{
	unsigned char buffer[512] = { 0 };
	extern const struct ssh_hash ssh_sha256;
	void* handle = ssh_sha256.init();
	ssh_sha256.bytes(handle, input.c_str(), input.length());
	ssh_sha256.final(handle, buffer);
	std::string out;
	base::Base64Encode((char*)buffer, &out);
	base64urlencodeNoPadding(out);
	return out;
}

static std::string randomDataBase64url()
{
	std::string ret = base::Generate256BitRandomBase64String();
	base64urlencodeNoPadding(ret);
	return ret;
}

int openUrlByStartProcess(char* const url)
{
	ShellExecuteA(NULL, "open",url, 0, 0, SW_SHOWDEFAULT);
	return 0;
}

void OnlineSessionManager::upload_file(string file)
{	
	std::string state = randomDataBase64url();
	string code_verifier = randomDataBase64url();
	string code_challenge = sha256(code_verifier);
	const char* const code_challenge_method = "S256";
	
	int proxytype = 0;
	char strAddr[256] = { 0 };
	bool bUserHttps = true;
	int get_proxy_return = getIEProxy("https://www.google.com", proxytype, strAddr, bUserHttps);

	MessageBoxA(NULL, 
		"1. how to auth: by google auth2 through default browser.\n"
		"2. with IE proxy:\n", 
		"U MAY WANT TO ASK", MB_OK);

	g_online_session_manager->mTcpServer.start();
	int port = g_online_session_manager->mTcpServer.getBindedPort();
	if (port < 0)
	{ 
		g_online_session_manager->mTcpServer.stop(); 
		return;
	}

	CURL *curl;
	CURLcode res;
	string response_str;
	char redirectBuff[128] = { 0 };
	snprintf(redirectBuff, sizeof(redirectBuff), "http://127.0.0.1:%d/", port);
	char requestUrl[4096] = { 0 };
	snprintf(requestUrl, sizeof(requestUrl), "https://accounts.google.com/o/oauth2/v2/auth?"
		"response_type=code&scope=openid%%20profile&redirect_uri=%s"
		"&client_id=%s"
		"&state=%s"
		"&code_challenge=%s"
		"&code_challenge_method=%s", 
		redirectBuff, client_id, state.c_str(), code_challenge.c_str(), code_challenge_method);
	
	openUrlByStartProcess(requestUrl);
	//curl = curl_easy_init();
	//assert(curl);
	//curl_easy_setopt(curl, CURLOPT_URL, requestUrl);
	//curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, query_auth_write_cb);
	//curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)&response_str); 
	//if (get_proxy_return >= 0)
	//{
	//	curl_easy_setopt(curl, CURLOPT_PROXY, strAddr);
	//	curl_easy_setopt(curl, CURLOPT_PROXYTYPE, proxytype);
	//}
	//
	//int count = 0;
	//while ((res = curl_easy_perform(curl)) != CURLE_OK && count < 5)
	//{
	//	response_str.clear();
	//	Sleep(1000);
	//	count++;
	//}
	//curl_easy_cleanup(curl);
	//g_ui_processor->process(NEW_PROCESSOR_JOB(OnlineSessionManager::upload_file_done, res == CURLE_OK, response_str));
	
}


void OnlineSessionManager::upload_file_done(bool is_success, string response)
{
	USES_CONVERSION;
	printf("%s", response.c_str());
	MessageBox(NULL, A2W(response.c_str()), L"test", MB_OK);
}

void OnlineSessionManager::download_sessions()
{

}