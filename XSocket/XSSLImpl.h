/*
 * Copyright: 7thTool Open Source <i7thTool@qq.com>
 * All rights reserved.
 * 
 * Author	: Scott
 * Email	：i7thTool@qq.com
 * Blog		: http://blog.csdn.net/zhangzq86
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */
#pragma once

#include "XSocketImpl.h"
#include "XStr.h"
#include <openssl/ssl.h>
#include <openssl/err.h>
#include <openssl/rand.h>

namespace XSocket {

enum {
    TLS_PROTO_TLSv1 = (1<<0),
    TLS_PROTO_TLSv1_1 = (1<<1),
    TLS_PROTO_TLSv1_2 = (1<<2),
    TLS_PROTO_TLSv1_3 =(1<<3),
    
    /* Use safe defaults */
    #ifdef TLS1_3_VERSION
    TLS_PROTO_DEFAULT = (TLS_PROTO_TLSv1_2|TLS_PROTO_TLSv1_3),
    #else
    TLS_PROTO_DEFAULT = (TLS_PROTO_TLSv1_2),
    #endif
};

typedef struct tagTLSContextConfig {
    char *cert_file;
    char *key_file;
    char *dh_params_file;
    char *ca_cert_file;
    char *ca_cert_dir;
    char *protocols;
    char *ciphers;
    char *ciphersuites;
    int prefer_server_ciphers;
} TLSContextConfig;

template<class TBase>
class SSLSocketT : public TBase
{
	typedef TBase Base;
protected:
    static SSL_CTX *tls_ctx_;
public:
    static void Init()
    {
        Base::Init();

        ERR_load_crypto_strings();
        SSL_load_error_strings();
        SSL_library_init();

        if (!RAND_poll())
        {
            PRINTF("OpenSSL: Failed to seed random number generator.");
        }
    }
    static void Term()
    {
        if(tls_ctx_) {
            SSL_CTX_free(tls_ctx_);
            tls_ctx_ = nullptr;
        }

        Base::Term();
    }
#ifdef _DEBUG
/**
 * Callback used for debugging
 */
static void sslLogCallback(const SSL *ssl, int where, int ret) {
    const char *retstr = "";
    int should_log = 1;
    /* Ignore low-level SSL stuff */

    if (where & SSL_CB_ALERT) {
        should_log = 1;
    }
    if (where == SSL_CB_HANDSHAKE_START || where == SSL_CB_HANDSHAKE_DONE) {
        should_log = 1;
    }
    if ((where & SSL_CB_EXIT) && ret == 0) {
        should_log = 1;
    }

    if (!should_log) {
        return;
    }

    retstr = SSL_alert_type_string(ret);
    printf("ST(0x%x). %s. R(0x%x)%s\n", where, SSL_state_string_long(ssl), ret, retstr);

    if (where == SSL_CB_HANDSHAKE_DONE) {
        printf("Using SSL version %s. Cipher=%s\n", SSL_get_version(ssl), SSL_get_cipher_name(ssl));
    }
}
#endif
/* Attempt to configure/reconfigure TLS. This operation is atomic and will
 * leave the SSL_CTX unchanged if fails.
 */
static int Configure() {
    char errbuf[256];
    SSL_CTX *ctx = NULL;
    ctx = SSL_CTX_new(SSLv23_method());
    if(!ctx) {
        ERR_error_string_n(ERR_get_error(), errbuf, sizeof(errbuf));
        PRINTF("Failed to configure ssl context: %s", errbuf);
    }
#ifdef _DEBUG
    SSL_CTX_set_info_callback(ctx, sslLogCallback);
#endif
    SSL_CTX_free(tls_ctx_);
    tls_ctx_ = ctx;
    return 0;
}
static int Configure(TLSContextConfig *ctx_config) {
    char errbuf[256];
    int protocols = 0;
    const char* tokens = nullptr;
    SSL_CTX *ctx = NULL;

    if (!ctx_config->cert_file) {
        PRINTF("No tls-cert-file configured!");
        goto error;
    }

    if (!ctx_config->key_file) {
        PRINTF("No tls-key-file configured!");
        goto error;
    }

    if (!ctx_config->ca_cert_file && !ctx_config->ca_cert_dir) {
        PRINTF("Either tls-ca-cert-file or tls-ca-cert-dir must be configured!");
        goto error;
    }

    ctx = SSL_CTX_new(SSLv23_method());
    #ifdef _DEBUG
        SSL_CTX_set_info_callback(ctx, sslLogCallback);
    #endif

    SSL_CTX_set_options(ctx, SSL_OP_NO_SSLv2|SSL_OP_NO_SSLv3);
    SSL_CTX_set_options(ctx, SSL_OP_SINGLE_DH_USE);

#ifdef SSL_OP_DONT_INSERT_EMPTY_FRAGMENTS
    SSL_CTX_set_options(ctx, SSL_OP_DONT_INSERT_EMPTY_FRAGMENTS);
#endif

    tokens = ctx_config->protocols;
    while(tokens)
    {
        if (0 == strnicmp(tokens, "tlsv1.1", 7)) { 
            protocols |= TLS_PROTO_TLSv1_1;
        } else if (0 == strnicmp(tokens, "tlsv1.2", 7)) { 
            protocols |= TLS_PROTO_TLSv1_2;
        } else if (0 == strnicmp(tokens, "tlsv1.3", 7)) {
#ifdef TLS1_3_VERSION
            protocols |= TLS_PROTO_TLSv1_3;
#else
            PRINTF("TLSv1.3 is specified in tls-protocols but not supported by OpenSSL.");
            protocols = -1;
            break;
#endif
        } else if (0 == strnicmp(tokens, "tlsv1", 5)) {
            protocols |= TLS_PROTO_TLSv1;
        } else {
            PRINTF("Invalid tls-protocols specified. "
                    "Use a combination of 'TLSv1', 'TLSv1.1', 'TLSv1.2' and 'TLSv1.3'.");
            protocols = -1;
            break;
        }
        tokens = strbrk(tokens, ' ');
        if(tokens) 
            tokens++;
    }
    if (protocols == -1) goto error;

    if (!(protocols & TLS_PROTO_TLSv1))
        SSL_CTX_set_options(ctx, SSL_OP_NO_TLSv1);
    if (!(protocols & TLS_PROTO_TLSv1_1))
        SSL_CTX_set_options(ctx, SSL_OP_NO_TLSv1_1);
#ifdef SSL_OP_NO_TLSv1_2
    if (!(protocols & TLS_PROTO_TLSv1_2))
        SSL_CTX_set_options(ctx, SSL_OP_NO_TLSv1_2);
#endif
#ifdef SSL_OP_NO_TLSv1_3
    if (!(protocols & TLS_PROTO_TLSv1_3))
        SSL_CTX_set_options(ctx, SSL_OP_NO_TLSv1_3);
#endif

#ifdef SSL_OP_NO_COMPRESSION
    SSL_CTX_set_options(ctx, SSL_OP_NO_COMPRESSION);
#endif

#ifdef SSL_OP_NO_CLIENT_RENEGOTIATION
    SSL_CTX_set_options(ssl->ctx, SSL_OP_NO_CLIENT_RENEGOTIATION);
#endif

    if (ctx_config->prefer_server_ciphers)
        SSL_CTX_set_options(ctx, SSL_OP_CIPHER_SERVER_PREFERENCE);

    SSL_CTX_set_mode(ctx, SSL_MODE_ENABLE_PARTIAL_WRITE|SSL_MODE_ACCEPT_MOVING_WRITE_BUFFER);
    SSL_CTX_set_verify(ctx, SSL_VERIFY_PEER|SSL_VERIFY_FAIL_IF_NO_PEER_CERT, NULL);
    SSL_CTX_set_ecdh_auto(ctx, 1);

    if (SSL_CTX_use_certificate_file(ctx, ctx_config->cert_file, SSL_FILETYPE_PEM) <= 0) {
        ERR_error_string_n(ERR_get_error(), errbuf, sizeof(errbuf));
        PRINTF("Failed to load certificate: %s: %s", ctx_config->cert_file, errbuf);
        goto error;
    }
        
    if (SSL_CTX_use_PrivateKey_file(ctx, ctx_config->key_file, SSL_FILETYPE_PEM) <= 0) {
        ERR_error_string_n(ERR_get_error(), errbuf, sizeof(errbuf));
        PRINTF("Failed to load private key: %s: %s", ctx_config->key_file, errbuf);
        goto error;
    }
    
    if (SSL_CTX_load_verify_locations(ctx, ctx_config->ca_cert_file, ctx_config->ca_cert_dir) <= 0) {
        ERR_error_string_n(ERR_get_error(), errbuf, sizeof(errbuf));
        PRINTF("Failed to configure CA certificate(s) file/directory: %s", errbuf);
        goto error;
    }

    if (ctx_config->dh_params_file) {
        FILE *dhfile = fopen(ctx_config->dh_params_file, "r");
        DH *dh = NULL;
        if (!dhfile) {
            PRINTF("Failed to load %s: %s", ctx_config->dh_params_file, Base::GetErrorMessage(Base::GetLastError(),errbuf,256));
            goto error;
        }

        dh = PEM_read_DHparams(dhfile, NULL, NULL, NULL);
        fclose(dhfile);
        if (!dh) {
            PRINTF("%s: failed to read DH params.", ctx_config->dh_params_file);
            goto error;
        }

        if (SSL_CTX_set_tmp_dh(ctx, dh) <= 0) {
            ERR_error_string_n(ERR_get_error(), errbuf, sizeof(errbuf));
            PRINTF("Failed to load DH params file: %s: %s", ctx_config->dh_params_file, errbuf);
            DH_free(dh);
            goto error;
        }

        DH_free(dh);
    }

    if (ctx_config->ciphers && !SSL_CTX_set_cipher_list(ctx, ctx_config->ciphers)) {
        PRINTF("Failed to configure ciphers: %s", ctx_config->ciphers);
        goto error;
    }

#ifdef TLS1_3_VERSION
    if (ctx_config->ciphersuites && !SSL_CTX_set_ciphersuites(ctx, ctx_config->ciphersuites)) {
        PRINTF("Failed to configure ciphersuites: %s", ctx_config->ciphersuites);
        goto error;
    }
#endif

    SSL_CTX_free(tls_ctx_);
    tls_ctx_ = ctx;

    return 0;

error:
    if (ctx) SSL_CTX_free(ctx);
    return -1;
}
protected:
    SSL *ssl_;
    
    /* Process the return code received from OpenSSL>
    * Update the want parameter with expected I/O.
    * Update the connection's error state if a real error has occured.
    * Returns an SSL error code, or 0 if no further handling is required.
    */
    static int handleSSLReturnCode(SSL *ssl, int ret_value, int *want_evt) {
        if (ret_value <= 0) {
            int ssl_err = SSL_get_error(ssl, ret_value);
            switch (ssl_err) {
                case SSL_ERROR_WANT_WRITE:
                    *want_evt |= FD_WRITE;
                    return 0;
                case SSL_ERROR_WANT_READ:
                    *want_evt |= FD_READ;
                    return 0;
                case SSL_ERROR_SYSCALL:
                    //conn->c.last_errno = errno;
                    //if (conn->ssl_error) zfree(conn->ssl_error);
                    //conn->ssl_error = errno ? zstrdup(strerror(errno)) : NULL;
                    break;
                default:
                    /* Error! */
                    // conn->c.last_errno = 0;
                    // if (conn->ssl_error) zfree(conn->ssl_error);
                    // conn->ssl_error = zmalloc(512);
                    // ERR_error_string_n(ERR_get_error(), conn->ssl_error, 512);
                    break;
            }

            return ssl_err;
        }

        return 0;
    }

public:
    SSLSocketT() {}
    ~SSLSocketT() {}

    inline SSL_CTX * GetTLSContext() { return tls_ctx_; }

    int Send(const char* lpBuf, int nBufLen, int nFlags = 0)
	{
        int ret, ssl_err;

        ERR_clear_error();

        ret = SSL_write(ssl_, lpBuf, nBufLen);
        if (ret <= 0) {
            int want = 0;
            if (!(ssl_err = handleSSLReturnCode(ssl_, ret, &want))) {
#ifdef WIN32
                SetLastError(WSAEWOULDBLOCK);
#else
                SetLastError(EAGAIN);
#endif
                return -1;
            } else {
                int nError = GetLastError();
                if (ssl_err == SSL_ERROR_ZERO_RETURN ||
                        ((ssl_err == SSL_ERROR_SYSCALL && !nError))) {
                    return 0;
                } else {
                    return -1;
                }
            }
        }

        return ret;
	}

	int Receive(char* lpBuf, int nBufLen, int nFlags = 0)
    {
        int ret, ssl_err;

        ERR_clear_error();

        ret = SSL_read(ssl_, lpBuf, nBufLen);
        if (ret <= 0) {
            int want = 0;
            if (!(ssl_err = handleSSLReturnCode(ssl_, ret, &want))) {
#ifdef WIN32
                SetLastError(WSAEWOULDBLOCK);
#else
                SetLastError(EAGAIN);
#endif
                return -1;
            } else {
                int nError = GetLastError();
                if (ssl_err == SSL_ERROR_ZERO_RETURN ||
                        ((ssl_err == SSL_ERROR_SYSCALL) && !nError)) {
                    return 0;
                } else {
                    return -1;
                }
            }
        }

        return ret;
    }
};

template<class TBase>
SSL_CTX * SSLSocketT<TBase>::tls_ctx_ = nullptr;

template<class TBase>
class SSLSocketExT : public SSLSocketT<TBase>
{
    typedef SSLSocketT<TBase> Base;
protected:
    SSL_CTX * ssl_ctx_ = nullptr;

public:
    SSLSocketExT():ssl_ctx_(Base::tls_ctx_)
    {

    }
    
    inline SSL_CTX * GetTLSContext() { return ssl_ctx_; }
    int SetTLSContext(const char *capath, const char *certpath = nullptr, const char *keypath = nullptr) {

        SSL_CTX *ssl_ctx = NULL;
        SSL *ssl = NULL;

        ssl_ctx = SSL_CTX_new(SSLv23_client_method());
        if (!ssl_ctx) {
            PRINTF("Failed to create SSL_CTX");
            goto error;
        }

    #ifdef _DEBUG
        SSL_CTX_set_info_callback(ssl_ctx, Base::sslLogCallback);
    #endif
        SSL_CTX_set_options(ssl_ctx, SSL_OP_NO_SSLv2 | SSL_OP_NO_SSLv3);
        SSL_CTX_set_verify(ssl_ctx, SSL_VERIFY_PEER, NULL);
        if ((certpath != NULL && keypath == NULL) || (keypath != NULL && certpath == NULL)) {
            PRINTF("certpath and keypath must be specified together");
            goto error;
        }

        if (capath) {
            if (!SSL_CTX_load_verify_locations(ssl_ctx, capath, NULL)) {
                PRINTF("Invalid CA certificate");
                goto error;
            }
        }
        if (certpath) {
            if (!SSL_CTX_use_certificate_chain_file(ssl_ctx, certpath)) {
                PRINTF("Invalid client certificate");
                goto error;
            }
            if (!SSL_CTX_use_PrivateKey_file(ssl_ctx, keypath, SSL_FILETYPE_PEM)) {
                PRINTF("Invalid client key");
                goto error;
            }
        }
        if(ssl_ctx_ != Base::tls_ctx_) SSL_CTX_free(ssl_ctx_);
        ssl_ctx_ = ssl_ctx;
        return 0;

    error:
        if (ssl_ctx) SSL_CTX_free(ssl_ctx);
        return -1;
    }
};

template<class TBase>
class SSLWorkSocketT : public TBase
{
	typedef TBase Base;
protected:
    byte require_auth_:1;
    byte ssl_accepted_:1;
public:
    SSLWorkSocketT():require_auth_(false),ssl_accepted_(false) {}
    ~SSLWorkSocketT() {}
    
	inline bool IsSSLAccepted() { return ssl_accepted_; }

protected:
    //
    inline int handleSSLAccept(int& nErrorCode)
    {
        ERR_clear_error();

        int ret = SSL_accept(Base::ssl_);
        if (ret <= 0) {
            int want = 0;
            if (!handleSSLReturnCode(Base::ssl_, ret, &want)) {
                Base::Select(want);

                nErrorCode = 
#ifdef WIN32
                (WSAEWOULDBLOCK);
#else
                (EAGAIN);
#endif
            } else {
#ifdef WIN32
                nErrorCode = WSAENOTCONN;
#else
                nErrorCode = ENOTCONN;
#endif
            }
            return SOCKET_ERROR;
        }
        ssl_accepted_ = true;
        return 0;
    }

    virtual void OnSSLAccept()
    {

    }

    virtual void OnRole(int nRole)
    {
        Base::OnRole(nRole);
        switch(nRole)
        {
        case SOCKET_ROLE_WORK:
        {
            Base::ssl_ = SSL_new(GetTLSContext());
            if (!require_auth_) {
                /* We still verify certificates if provided, but don't require them.
                 */
                SSL_set_verify(Base::ssl_, SSL_VERIFY_PEER, NULL);
            }

            SSL_set_fd(Base::ssl_, (SOCKET)*this);
            SSL_set_accept_state(Base::ssl_);
        }
        break;
        default:
        ASSERT(0);
        break;
        }
    }

	virtual void OnReceive(int nErrorCode)
    {
        if(nErrorCode) {
            Base::OnReceive(nErrorCode);
            return;
        }

        if(!IsSSLAccepted()) {
            handleSSLAccept(nErrorCode);
            if(IsSSLAccepted()) {
                OnSSLAccept();
                return;
            }
        }
        Base::OnReceive(nErrorCode);
    }
    virtual void OnReceive(const char* lpBuf, int nBufLen, int nFlags) 
	{
		Base::OnReceive(lpBuf, nBufLen, nFlags);
    }

	virtual void OnSend(int nErrorCode)
    {
        if(nErrorCode) {
            Base::OnSend(nErrorCode);
            return;
        }

        if(!IsSSLAccepted()) {
            handleSSLAccept(nErrorCode);
            if(IsSSLAccepted()) {
                OnSSLAccept();
                return;
            }
        }
        Base::OnSend(nErrorCode);
    }
	virtual void OnSend(const char* lpBuf, int nBufLen, int nFlags)
    {
        Base::OnSend(lpBuf, nBufLen, nFlags);
    }
};


template<class TBase>
class SSLConnectSocketT : public TBase
{
	typedef TBase Base;
protected:
    byte ssl_connected_:1;
public:
    SSLConnectSocketT():ssl_connected_(false) {}
    ~SSLConnectSocketT() {}

    inline bool IsSSLConnected() { return ssl_connected_; }

protected:
    //
    inline int handleSSLConnect(int& nErrorCode)
    {
        ERR_clear_error();

        int ret = SSL_connect(Base::ssl_);
        if (ret <= 0) {
            int want = 0;
            if (!handleSSLReturnCode(Base::ssl_, ret, &want)) {
                Base::Select(want);

                /* Avoid hitting UpdateSSLEvent, which knows nothing
                 * of what SSL_connect() wants and instead looks at our
                 * R/W handlers.
                 */
                nErrorCode = 
#ifdef WIN32
                (WSAEWOULDBLOCK);
#else
                (EAGAIN);
#endif
            } else {
#ifdef WIN32
                nErrorCode = WSAENOTCONN;
#else
                nErrorCode = ENOTCONN;
#endif
            }
            return SOCKET_ERROR;
        }
        ssl_connected_ = true;
        return 0;
    }

    virtual void OnSSLConnect()
    {
        
    }

    virtual void OnRole(int nRole)
    {
        Base::OnRole(nRole);
        switch(nRole)
        {
        case SOCKET_ROLE_CONNECT:
        {
            Base::ssl_ = SSL_new(GetTLSContext());

            SSL_set_fd(Base::ssl_, (SOCKET)*this);
            SSL_set_connect_state(Base::ssl_);
        }
        break;
        default:
        ASSERT(0);
        break;
        }
    }

    virtual void OnReceive(int nErrorCode)
    {
        if(nErrorCode) {
            Base::OnReceive(nErrorCode);
            return;
        }

        if(!IsSSLConnected()) {
            handleSSLConnect(nErrorCode);
            if(IsSSLConnected()) {
                OnSSLConnect();
            }
        } else {
            Base::OnReceive(nErrorCode);
        }
    }
    virtual void OnReceive(const char* lpBuf, int nBufLen, int nFlags) 
	{
		Base::OnReceive(lpBuf, nBufLen, nFlags);
    }

	virtual void OnSend(int nErrorCode)
    {
        if(nErrorCode) {
            Base::OnSend(nErrorCode);
            return;
        }

        if(!IsSSLConnected()) {
            handleSSLConnect(nErrorCode);
            if(IsSSLConnected()) {
                OnSSLConnect();
            }
        } else {
            Base::OnSend(nErrorCode);
        }
    }
	virtual void OnSend(const char* lpBuf, int nBufLen, int nFlags)
    {
        Base::OnSend(lpBuf, nBufLen, nFlags);
    }

    virtual void OnConnect(int nErrorCode)
    {
        if (nErrorCode) {
            Base::OnConnect(nErrorCode);
            return;
        }

        Base::OnConnect(nErrorCode);
        ASSERT(Base::IsConnected());
        handleSSLConnect(nErrorCode);
        if(IsSSLConnected()) {
            OnSSLConnect();
        }
    }
};

};