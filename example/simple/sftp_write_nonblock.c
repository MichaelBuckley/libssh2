/*
 * $Id: sftp_write_nonblock.c,v 1.1 2007/04/22 15:57:52 jehousley Exp $
 *
 * Sample showing how to do SFTP non-blocking write transfers.
 *
 * The sample code has default values for host name, user name, password
 * and path to copy, but you can specify them on the command line like:
 *
 * "sftp 192.168.0.1 user password sftp_write_nonblock.c /tmp/sftp_write_nonblock.c"
 */

#include <libssh2.h>
#include <libssh2_sftp.h>

#ifndef WIN32
# include <netinet/in.h>
# include <sys/socket.h>
# include <unistd.h>
# include <arpa/inet.h>
#else
# include <winsock2.h>
#endif



#include <sys/types.h>
#include <fcntl.h>
#include <errno.h>
#include <stdio.h>
#include <ctype.h>

int main(int argc, char *argv[])
{
    unsigned long hostaddr;
    int sock, i, auth_pw = 1;
    struct sockaddr_in sin;
    const char *fingerprint;
    LIBSSH2_SESSION *session;
    char *username=(char *)"username";
    char *password=(char *)"password";
    char *loclfile=(char *)"sftp_write_nonblock.c";
    char *sftppath=(char *)"/tmp/sftp_write_nonblock.c";
    int rc;
    FILE *local;
    LIBSSH2_SFTP *sftp_session;
    LIBSSH2_SFTP_HANDLE *sftp_handle;
    char mem[1024];
    size_t nread;
    char *ptr;

#ifdef WIN32
    WSADATA wsadata;

    WSAStartup(WINSOCK_VERSION, &wsadata);
#endif

    if (argc > 1) {
        hostaddr = inet_addr(argv[1]);
    } else {
        hostaddr = htonl(0x7F000001);
    }

    if(argc > 2) {
        username = argv[2];
    }
    if(argc > 3) {
        password = argv[3];
    }
    if(argc > 4) {
        loclfile = argv[4];
    }
    if(argc > 5) {
        sftppath = argv[5];
    }
    
    local = fopen(loclfile, "rb");
    if (!local) {
        printf("Can't local file %s\n", loclfile);
        goto shutdown;
    }
    
    /*
     * The application code is responsible for creating the socket
     * and establishing the connection
     */
    sock = socket(AF_INET, SOCK_STREAM, 0);

    sin.sin_family = AF_INET;
    sin.sin_port = htons(22);
    sin.sin_addr.s_addr = hostaddr;
    if (connect(sock, (struct sockaddr*)(&sin), 
            sizeof(struct sockaddr_in)) != 0) {
        fprintf(stderr, "failed to connect!\n");
        return -1;
    }

    /* We set the socket non-blocking. We do it after the connect just to
       simplify the example code. */
#ifdef F_SETFL
    /* FIXME: this can/should be done in a more portable manner */
    rc = fcntl(sock, F_GETFL, 0);
    fcntl(sock, F_SETFL, rc | O_NONBLOCK);
#else
#error "add support for setting the socket non-blocking here"
#endif

    /* Create a session instance
     */
    session = libssh2_session_init();
    if(!session)
        return -1;

    /* ... start it up. This will trade welcome banners, exchange keys,
     * and setup crypto, compression, and MAC layers
     */
    rc = libssh2_session_startup(session, sock);
    if(rc) {
        fprintf(stderr, "Failure establishing SSH session: %d\n", rc);
        return -1;
    }

    /* At this point we havn't yet authenticated.  The first thing to do
     * is check the hostkey's fingerprint against our known hosts Your app
     * may have it hard coded, may go to a file, may present it to the
     * user, that's your call
     */
    fingerprint = libssh2_hostkey_hash(session, LIBSSH2_HOSTKEY_HASH_MD5);
    printf("Fingerprint: ");
    for(i = 0; i < 16; i++) {
        printf("%02X ", (unsigned char)fingerprint[i]);
    }
    printf("\n");

    if (auth_pw) {
        /* We could authenticate via password */
        if (libssh2_userauth_password(session, username, password)) {
            printf("Authentication by password failed.\n");
            goto shutdown;
        }
    } else {
        /* Or by public key */
        if (libssh2_userauth_publickey_fromfile(session, username,
                            "/home/username/.ssh/id_rsa.pub",
                            "/home/username/.ssh/id_rsa",
                            password)) {
            printf("\tAuthentication by public key failed\n");
            goto shutdown;
        }
    }

    fprintf(stderr, "libssh2_sftp_init()!\n");
    sftp_session = libssh2_sftp_init(session);

    if (!sftp_session) {
        fprintf(stderr, "Unable to init SFTP session\n");
        goto shutdown;
    }

    /* Since we have set non-blocking, tell libssh2 we are non-blocking */
    libssh2_sftp_set_blocking(sftp_session, 0);
    
    fprintf(stderr, "libssh2_sftp_open()!\n");
    /* Request a file via SFTP */
    sftp_handle =
        libssh2_sftp_open(sftp_session, sftppath,
                      LIBSSH2_FXF_WRITE|LIBSSH2_FXF_CREAT,
                      LIBSSH2_SFTP_S_IRUSR|LIBSSH2_SFTP_S_IWUSR|
                      LIBSSH2_SFTP_S_IRGRP|LIBSSH2_SFTP_S_IROTH);
    
    if (!sftp_handle) {
        fprintf(stderr, "Unable to open file with SFTP\n");
        goto shutdown;
    }
    fprintf(stderr, "libssh2_sftp_open() is done, now send data!\n");
    do {
        nread = fread(mem, 1, sizeof(mem), local);
        if (nread <= 0) {
            /* end of file */
            break;
        }
        ptr = mem;
        
        do {
            /* write data in a loop until we block */
            while ((rc = libssh2_sftp_writenb(sftp_handle, ptr, nread)) == LIBSSH2SFTP_EAGAIN) {
                ;
            }
            ptr += rc;
            nread -= nread;
        } while (rc > 0);
    } while (1);

    fclose(local);
    libssh2_sftp_close(sftp_handle);
    libssh2_sftp_shutdown(sftp_session);

 shutdown:

    libssh2_session_disconnect(session, "Normal Shutdown, Thank you for playing");
    libssh2_session_free(session);

#ifdef WIN32
    Sleep(1000);
    closesocket(sock);
#else
    sleep(1);
    close(sock);
#endif
printf("all done\n");
    return 0;
}
