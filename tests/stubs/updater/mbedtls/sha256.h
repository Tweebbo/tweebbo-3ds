#pragma once
#include <openssl/evp.h>
#include <stddef.h>
typedef struct {EVP_MD_CTX *value;} mbedtls_sha256_context;
static inline void mbedtls_sha256_init(mbedtls_sha256_context *c){c->value=NULL;}
static inline int mbedtls_sha256_starts_ret(mbedtls_sha256_context *c,int is224){(void)is224;c->value=EVP_MD_CTX_new();return c->value&&EVP_DigestInit_ex(c->value,EVP_sha256(),NULL)==1?0:-1;}
static inline int mbedtls_sha256_update_ret(mbedtls_sha256_context *c,const unsigned char *p,size_t n){return EVP_DigestUpdate(c->value,p,n)==1?0:-1;}
static inline int mbedtls_sha256_finish_ret(mbedtls_sha256_context *c,unsigned char out[32]){return EVP_DigestFinal_ex(c->value,out,NULL)==1?0:-1;}
static inline void mbedtls_sha256_free(mbedtls_sha256_context *c){EVP_MD_CTX_free(c->value);c->value=NULL;}
