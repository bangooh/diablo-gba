#define _POSIX_C_SOURCE 200809L
#define _CRT_SECURE_NO_WARNINGS
#include "builder.h"
#include "config.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <bcrypt.h>
#include <io.h>
#include <sys/stat.h>
#define OPEN _wfopen
#else
#ifdef BUILDER_PORTABLE_CRYPTO
#include "crypto.h"
#else
#include <openssl/evp.h>
#endif
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#define OPEN fopen
#endif

typedef struct {
#ifdef _WIN32
    BCRYPT_ALG_HANDLE alg;
    BCRYPT_HASH_HANDLE ctx;
#elif defined(BUILDER_PORTABLE_CRYPTO)
    void *ctx;
#else
    EVP_MD_CTX *ctx;
#endif
} Hash;
typedef struct { Progress callback; void *userdata; } Job;
static void wipe(void *p, size_t n) { volatile unsigned char *v=p; while (n--) *v++=0; }
static int report(Job *j, enum Stage stage, unsigned percent) {
    return !j->callback || j->callback(stage,percent,j->userdata);
}
static int hash_start(Hash *h) {
    memset(h,0,sizeof(*h));
#ifdef _WIN32
    if (BCryptOpenAlgorithmProvider(&h->alg,BCRYPT_SHA256_ALGORITHM,NULL,0)<0) return 0;
    if (BCryptCreateHash(h->alg,&h->ctx,NULL,0,NULL,0,0)<0) {
        BCryptCloseAlgorithmProvider(h->alg,0); return 0;
    }
#elif defined(BUILDER_PORTABLE_CRYPTO)
    h->ctx=portable_hash_new();
    if (!h->ctx) return 0;
#else
    h->ctx=EVP_MD_CTX_new();
    if (!h->ctx) return 0;
    if (EVP_DigestInit_ex(h->ctx,EVP_sha256(),NULL)!=1) {
        EVP_MD_CTX_free(h->ctx); return 0;
    }
#endif
    return 1;
}
static int hash_add(Hash *h,const void *p,size_t n) {
#ifdef _WIN32
    return BCryptHashData(h->ctx,(PUCHAR)p,(ULONG)n,0)>=0;
#elif defined(BUILDER_PORTABLE_CRYPTO)
    portable_hash_add(h->ctx,p,n); return 1;
#else
    return EVP_DigestUpdate(h->ctx,p,n)==1;
#endif
}
static int hash_end(Hash *h,unsigned char out[32]) {
    int ok;
#ifdef _WIN32
    ok=BCryptFinishHash(h->ctx,out,32,0)>=0;
    BCryptDestroyHash(h->ctx); BCryptCloseAlgorithmProvider(h->alg,0);
#elif defined(BUILDER_PORTABLE_CRYPTO)
    portable_hash_end(h->ctx,out); ok=1;
#else
    unsigned n=0; ok=EVP_DigestFinal_ex(h->ctx,out,&n)==1 && n==32;
    EVP_MD_CTX_free(h->ctx);
#endif
    return ok;
}
static int sha(const void *p,size_t n,unsigned char out[32]) {
    Hash h; if (!hash_start(&h)) return 0;
    int ok=hash_add(&h,p,n); return hash_end(&h,out) && ok;
}
static int file_size(FILE *f,uint64_t *out) {
#ifdef _WIN32
    struct _stat64 s;
    if (_fstat64(_fileno(f),&s) || (s.st_mode&_S_IFMT)!=_S_IFREG || s.st_size<0) return 0;
#else
    struct stat s;
    if (fstat(fileno(f),&s) || !S_ISREG(s.st_mode) || s.st_size<0) return 0;
#endif
    *out=(uint64_t)s.st_size; return 1;
}
static int exists(const pathchar *p) {
#ifdef _WIN32
    DWORD attrs=GetFileAttributesW(p);
    return attrs!=INVALID_FILE_ATTRIBUTES;
#else
    struct stat s; return lstat(p,&s)==0;
#endif
}
static int gba_extension(const pathchar *p) {
#ifdef _WIN32
    size_t n=wcslen(p); return n>=4 && !_wcsicmp(p+n-4,L".gba");
#else
    size_t n=strlen(p);
    return n>=4 && p[n-4]=='.' && (p[n-3]=='g'||p[n-3]=='G') &&
        (p[n-2]=='b'||p[n-2]=='B') && (p[n-1]=='a'||p[n-1]=='A');
#endif
}

static int decrypt(const unsigned char key[32],const unsigned char *header,
                   const unsigned char *cipher,const unsigned char *tag,unsigned char *plain,size_t bytes);
static int mpq_key(const pathchar *p,unsigned char key[32],Job *j) {
    FILE *f=OPEN(p,PATH_TEXT("rb")); if (!f) return RESULT_MPQ_INVALID;
    uint64_t size=0,total=0; unsigned char sig[4];
    if (!file_size(f,&size) || (size!=MPQ_SIZE && size!=MPQ_GOG_SIZE) || fread(sig,1,4,f)!=4 ||
        memcmp(sig,"MPQ\x1a",4) || fseek(f,0,SEEK_SET)) { fclose(f); return RESULT_MPQ_INVALID; }
    Hash h; unsigned char buf[65536],digest[32],material[sizeof(KEY_DOMAIN)-1+32];
    if (!hash_start(&h)) { fclose(f); return RESULT_MEMORY; }
    int result=RESULT_OK; unsigned previous=101; size_t n;
    while ((n=fread(buf,1,sizeof(buf),f))>0) {
        total+=n;
        if (!hash_add(&h,buf,n)) { result=RESULT_MPQ_INVALID; break; }
        unsigned percent=(unsigned)(total*70/size);
        if (percent!=previous) {
            previous=percent;
            if (!report(j,STAGE_MPQ,percent)) { result=RESULT_CANCELLED; break; }
        }
    }
    if (ferror(f) || (result==RESULT_OK && total!=size)) result=RESULT_MPQ_INVALID;
    fclose(f);
    if (!hash_end(&h,digest) && result==RESULT_OK) result=RESULT_MPQ_INVALID;
    if (result==RESULT_OK) {
        memcpy(material,KEY_DOMAIN,sizeof(KEY_DOMAIN)-1);
        memcpy(material+sizeof(KEY_DOMAIN)-1,digest,32);
        if (!sha(material,sizeof(material),key)) result=RESULT_MEMORY;
    }
    /* GOG authenticates a small wrapped payload key. Neither edition's MPQ
       digest nor a plaintext payload key is embedded in the application. */
    if (result==RESULT_OK && size==MPQ_GOG_SIZE) {
        unsigned char unwrapped[32]={0};
        if (!decrypt(key,gog_key_header,gog_key_cipher,gog_key_tag,unwrapped,sizeof(unwrapped)))
            result=RESULT_AUTHENTICATION;
        else memcpy(key,unwrapped,sizeof(unwrapped));
        wipe(unwrapped,sizeof(unwrapped));
    }
    wipe(material,sizeof(material)); wipe(digest,32); wipe(buf,sizeof(buf));
    return result;
}
static int decrypt(const unsigned char key[32],const unsigned char *header,
                   const unsigned char *cipher,const unsigned char *tag,unsigned char *plain,size_t bytes) {
#ifdef _WIN32
    BCRYPT_ALG_HANDLE alg=NULL; BCRYPT_KEY_HANDLE k=NULL;
    BCRYPT_AUTHENTICATED_CIPHER_MODE_INFO info; ULONG n=0; int ok=0;
    if (BCryptOpenAlgorithmProvider(&alg,BCRYPT_AES_ALGORITHM,NULL,0)<0) goto done;
    if (BCryptSetProperty(alg,BCRYPT_CHAINING_MODE,(PUCHAR)BCRYPT_CHAIN_MODE_GCM,
                         sizeof(BCRYPT_CHAIN_MODE_GCM),0)<0) goto done;
    if (BCryptGenerateSymmetricKey(alg,&k,NULL,0,(PUCHAR)key,32,0)<0) goto done;
    BCRYPT_INIT_AUTH_MODE_INFO(info);
    info.pbNonce=(PUCHAR)header+8; info.cbNonce=12;
    info.pbAuthData=(PUCHAR)header; info.cbAuthData=HEADER_SIZE;
    info.pbTag=(PUCHAR)tag; info.cbTag=16;
    ok=BCryptDecrypt(k,(PUCHAR)cipher,(ULONG)bytes,&info,NULL,0,plain,(ULONG)bytes,&n,0)>=0 && n==bytes;
done:
    if (k) BCryptDestroyKey(k);
    if (alg) BCryptCloseAlgorithmProvider(alg,0);
    return ok;
#elif defined(BUILDER_PORTABLE_CRYPTO)
    return portable_decrypt(key,header,cipher,tag,plain,bytes);
#else
    EVP_CIPHER_CTX *c=EVP_CIPHER_CTX_new(); int n=0,last=0,ok=0;
    if (!c) return 0;
    if (EVP_DecryptInit_ex(c,EVP_aes_256_gcm(),NULL,NULL,NULL)!=1) goto done;
    if (EVP_CIPHER_CTX_ctrl(c,EVP_CTRL_GCM_SET_IVLEN,12,NULL)!=1) goto done;
    if (EVP_DecryptInit_ex(c,NULL,NULL,key,header+8)!=1) goto done;
    if (EVP_DecryptUpdate(c,NULL,&n,header,HEADER_SIZE)!=1) goto done;
    if (EVP_DecryptUpdate(c,plain,&n,cipher,(int)bytes)!=1 || n!=(int)bytes) goto done;
    if (EVP_CIPHER_CTX_ctrl(c,EVP_CTRL_GCM_SET_TAG,16,(void*)tag)!=1) goto done;
    ok=EVP_DecryptFinal_ex(c,plain+n,&last)==1 && last==0;
done:
    EVP_CIPHER_CTX_free(c); return ok;
#endif
}
/* Publish a completely written, flushed ROM without replacing any existing file.
   A sibling .partial file may remain if the OS/process is abruptly terminated. */
static int save_new(const pathchar *output,const unsigned char *rom,Job *j) {
    int result=RESULT_OUTPUT;
#ifdef _WIN32
    size_t len=wcslen(output);
    wchar_t *temp=calloc(len+64,sizeof(wchar_t)); if (!temp) return RESULT_MEMORY;
    unsigned char random[12];
    if (BCryptGenRandom(NULL,random,sizeof(random),BCRYPT_USE_SYSTEM_PREFERRED_RNG)<0) { free(temp); return RESULT_OUTPUT; }
    wcscpy(temp,output); wcscat(temp,L".");
    for (unsigned i=0;i<sizeof(random);i++) swprintf(temp+len+1+2*i,3,L"%02x",random[i]);
    wcscat(temp,L".partial");
    HANDLE f=CreateFileW(temp,GENERIC_WRITE,0,NULL,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,NULL);
    if (f==INVALID_HANDLE_VALUE) { free(temp); return RESULT_OUTPUT; }
    for (unsigned off=0;off<ROM_SIZE;off+=262144) {
        if (!report(j,STAGE_SAVE,95+off*4u/ROM_SIZE)) { result=RESULT_CANCELLED; goto fail; }
        DWORD count=0;
        if (!WriteFile(f,rom+off,262144,&count,NULL) || count!=262144) goto fail;
    }
    if (!FlushFileBuffers(f)) goto fail;
    if (!CloseHandle(f)) { f=INVALID_HANDLE_VALUE; goto fail; }
    f=INVALID_HANDLE_VALUE;
    if (!report(j,STAGE_SAVE,99)) { result=RESULT_CANCELLED; goto fail; }
    if (!MoveFileExW(temp,output,MOVEFILE_WRITE_THROUGH)) {
        DWORD err=GetLastError();
        if (err==ERROR_ALREADY_EXISTS || err==ERROR_FILE_EXISTS || exists(output)) result=RESULT_OUTPUT_EXISTS;
        goto fail;
    }
    free(temp); return RESULT_OK;
fail:
    if (f!=INVALID_HANDLE_VALUE) CloseHandle(f);
    DeleteFileW(temp); free(temp); return result;
#else
    size_t len=strlen(output);
    char *temp=malloc(len+32); if (!temp) return RESULT_MEMORY;
    snprintf(temp,len+32,"%s.partial.XXXXXX",output);
    int fd=mkstemp(temp); if (fd<0) { free(temp); return RESULT_OUTPUT; }
    for (unsigned off=0;off<ROM_SIZE;) {
        if (!report(j,STAGE_SAVE,95+off*4u/ROM_SIZE)) { result=RESULT_CANCELLED; goto fail; }
        size_t chunk=ROM_SIZE-off; if (chunk>262144) chunk=262144;
        ssize_t n=write(fd,rom+off,chunk);
        if (n<0 && errno==EINTR) continue;
        if (n<=0) goto fail;
        off+=(unsigned)n;
    }
    if (fsync(fd)) goto fail;
    if (close(fd)) { fd=-1; goto fail; } fd=-1;
    if (!report(j,STAGE_SAVE,99)) { result=RESULT_CANCELLED; goto fail; }
    /* link is an atomic no-replace publication on the same filesystem. */
    if (link(temp,output)) { if (errno==EEXIST) result=RESULT_OUTPUT_EXISTS; goto fail; }
    unlink(temp); free(temp); return RESULT_OK;
fail:
    if (fd>=0) close(fd);
    unlink(temp); free(temp); return result;
#endif
}

static int builder_run_impl(const pathchar *mpq,const pathchar *package,
                            const unsigned char *memory,size_t memory_size,
                            const pathchar *output,Progress callback,void *userdata) {
    Job job={callback,userdata};
    unsigned char header[HEADER_SIZE],tag[16],key[32]={0},digest[32];
    unsigned char *owned_cipher=NULL,*plain=NULL; const unsigned char *cipher=NULL; FILE *f=NULL;
    int result=RESULT_PACKAGE_INVALID; uint64_t size;
    if (!report(&job,STAGE_CHECK,0)) return RESULT_CANCELLED;
    if (!gba_extension(output)) return RESULT_OUTPUT_EXTENSION;
    if (exists(output)) return RESULT_OUTPUT_EXISTS;
    if (memory) {
        if (memory_size!=HEADER_SIZE+ROM_SIZE+16u) goto done;
        memcpy(header,memory,HEADER_SIZE);
    } else {
        f=OPEN(package,PATH_TEXT("rb")); if (!f) return RESULT_PACKAGE_MISSING;
        if (!file_size(f,&size) || size!=HEADER_SIZE+ROM_SIZE+16u ||
            fread(header,1,HEADER_SIZE,f)!=HEADER_SIZE) goto done;
    }
    if (memcmp(header,PACK_MAGIC,8) || memcmp(header+20,expected_rom_hash,32)) goto done;
    for (unsigned i=52;i<HEADER_SIZE;i++) if (header[i]) goto done;
    result=mpq_key(mpq,key,&job); if (result) goto done;
    result=RESULT_MEMORY; plain=malloc(ROM_SIZE);
    if (!plain) goto done;
    if (memory) {
        cipher=memory+HEADER_SIZE;
        memcpy(tag,memory+HEADER_SIZE+ROM_SIZE,16);
        if (!report(&job,STAGE_PACKAGE,80)) { result=RESULT_CANCELLED; goto done; }
    } else {
        owned_cipher=malloc(ROM_SIZE); if (!owned_cipher) goto done;
        cipher=owned_cipher;
        result=RESULT_PACKAGE_INVALID;
        for (unsigned off=0;off<ROM_SIZE;off+=262144) {
            if (!report(&job,STAGE_PACKAGE,70+off*10u/ROM_SIZE)) { result=RESULT_CANCELLED; goto done; }
            if (fread(owned_cipher+off,1,262144,f)!=262144) goto done;
        }
        if (fread(tag,1,16,f)!=16 || fgetc(f)!=EOF || ferror(f)) goto done;
        fclose(f); f=NULL;
    }
    result=RESULT_CANCELLED; if (!report(&job,STAGE_DECRYPT,82)) goto done;
    result=RESULT_AUTHENTICATION; if (!decrypt(key,header,cipher,tag,plain,ROM_SIZE)) goto done;
    result=RESULT_CANCELLED; if (!report(&job,STAGE_VERIFY,90)) goto done;
    result=RESULT_ROM_INVALID;
    if (!sha(plain,ROM_SIZE,digest) || memcmp(digest,expected_rom_hash,32)) goto done;
    result=RESULT_CANCELLED; if (!report(&job,STAGE_SAVE,95)) goto done;
    result=save_new(output,plain,&job);
    if (!result) (void)report(&job,STAGE_DONE,100);
done:
    if (f) fclose(f);
    wipe(key,32); wipe(digest,32);
    if (plain) { wipe(plain,ROM_SIZE); free(plain); }
    free(owned_cipher); return result;
}
int builder_run(const pathchar *mpq,const pathchar *package,const pathchar *output,
                Progress callback,void *userdata) {
    return builder_run_impl(mpq,package,NULL,0,output,callback,userdata);
}
int builder_run_memory(const pathchar *mpq,const unsigned char *package,size_t size,
                       const pathchar *output,Progress callback,void *userdata) {
    if (!package) return RESULT_PACKAGE_INVALID;
    return builder_run_impl(mpq,NULL,package,size,output,callback,userdata);
}
const char *builder_stage(enum Stage s) {
    static const char *names[]={"Checking files", "Reading your MPQ", "Reading data package",
        "Authenticating and restoring", "Verifying V0.9.5 ROM", "Saving ROM", "Complete"};
    return s>=STAGE_CHECK && s<=STAGE_DONE ? names[s] : "Working";
}
const char *builder_message(int code) {
    static const char *messages[]={
        "ROM created. SHA-256 matches the verified V0.9.5 build.",
        "ROM data is unavailable. Download a fresh copy of the program.",
        "ROM data is invalid, truncated or damaged. Download a fresh copy of the program.",
        "Memory or cryptographic provider could not be initialized.",
        "Cannot read DIABDAT.MPQ. Supported: Diablo 1.0 (517,501,242 bytes) or GOG (517,501,282 bytes).",
        "This MPQ edition is unsupported, or the data package was changed. No ROM was saved.",
        "ROM checksum failed. No ROM was saved.",
        "Cannot save ROM. Choose a writable local folder with at least 40 MB free.",
        "Cancelled. No new ROM was published.",
        "Output already exists. Choose a new name; existing files are never replaced.",
        "Choose an output filename ending in .gba."
    };
    return code>=0 && code<(int)(sizeof(messages)/sizeof(messages[0])) ? messages[code] : "Unknown error";
}
