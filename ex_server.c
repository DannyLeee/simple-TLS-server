#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <openssl/ssl.h>
#include <openssl/err.h>

// set the ca path here
#define CA_CERT "ca.crt"

// set the certificate path here
// right certificate
#define RIGHT_CERT "host.crt"
#define RIGHT_KEY "host.key"
// wrong certificate
#define WRONG_CERT "wrong.crt"
#define WRONG_KEY "wrong.key"

int create_socket(int port)
{
    int s;
    struct sockaddr_in addr;

    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);

    s = socket(AF_INET, SOCK_STREAM, 0);
    if (s < 0)
    {
	    perror("Unable to create socket");
	    exit(EXIT_FAILURE);
    }

    if (bind(s, (struct sockaddr*)&addr, sizeof(addr)) < 0)
    {
        perror("Unable to bind");
        exit(EXIT_FAILURE);
    }

    if (listen(s, 1) < 0)
    {
        perror("Unable to listen");
        exit(EXIT_FAILURE);
    }

    return s;
}

// 創建 SSL context
SSL_CTX *create_context()
{
    const SSL_METHOD *method;
    SSL_CTX *ctx;

    method = TLSv1_2_server_method();    // create 的方法

    ctx = SSL_CTX_new(method);
    if (!ctx) {
	perror("Unable to create SSL context");
	ERR_print_errors_fp(stderr);
	exit(EXIT_FAILURE);
    }

    return ctx;
}

// 配置 SSL context
void configure_context(SSL_CTX *ctx, const char *cert, const char *key)
{
    SSL_CTX_set_ecdh_auto(ctx, 1);  // 選擇橢圓曲線

    /* Set the key and cert */
    if (SSL_CTX_use_certificate_file(ctx, cert, SSL_FILETYPE_PEM) <= 0) {
        ERR_print_errors_fp(stderr);
	exit(EXIT_FAILURE);
    }

    if (SSL_CTX_use_PrivateKey_file(ctx, key, SSL_FILETYPE_PEM) <= 0 ) {
        ERR_print_errors_fp(stderr);
	exit(EXIT_FAILURE);
    }
    SSL_CTX_load_verify_locations(ctx, CA_CERT, NULL);
    SSL_CTX_set_verify(ctx, SSL_VERIFY_PEER, NULL);
}

void ShowCerts(SSL* ssl)
{   
    X509 *cert; // Certificate display and signing utility
    char *line;

    cert = SSL_get_peer_certificate(ssl); /* get the server's certificate */
    if ( cert != NULL )
    {
        printf("Client certificates:\n");
        line = X509_NAME_oneline(X509_get_subject_name(cert), 0, 0);
        printf("Subject: %s\n", line);
        free(line);       /* free the malloc'ed string */
        line = X509_NAME_oneline(X509_get_issuer_name(cert), 0, 0);
        printf("Issuer: %s\n", line);
        free(line);       /* free the malloc'ed string */
        X509_free(cert);     /* free the malloc'ed certificate copy */
    }
    else
        printf("Info: No client certificates configured.\n");
}

int main(int argc, char *argv[])
{
    setvbuf(stdout, NULL, _IONBF, 0);   // 把 STDOUT buffer 拿掉
    int sock;
    SSL_CTX *ctx;

    char * _CERT;
    char * _KEY;
    switch (argc)
    {
    case 1:
        _CERT = RIGHT_CERT;
        _KEY =  RIGHT_KEY;
        break;
    case 2:
        _CERT = (strcmp(argv[1], "wrong") == 0) ? WRONG_CERT : RIGHT_CERT;
        _KEY = (strcmp(argv[1], "wrong") == 0) ? WRONG_KEY : RIGHT_KEY;
        break;
    default:
        fprintf(stderr, "wrong argument number\n");
        exit(EXIT_FAILURE);
        break;
    }

    // 初始化 openssl
    SSL_library_init();
    ctx = create_context();
    configure_context(ctx, _CERT, _KEY);
    sock = create_socket(8787);

    /* Handle connections */
    while(1) 
    {
        struct sockaddr_in addr;
        uint len = sizeof(addr);
        SSL *ssl;
        char *reply;
        char receive[1024];
        int count;
        FILE *fp;

        int client = accept(sock, (struct sockaddr*)&addr, &len);
        if (client < 0)
        {
            perror("Unable to accept");
            exit(EXIT_FAILURE);
        }

        ssl = SSL_new(ctx);
        SSL_set_fd(ssl, client);    // 配對 SSL 跟新的連線 fd
        SSL_set_verify_depth(ssl, 1);


        // SSL_accept() 處理 TSL handshake
        if (SSL_accept(ssl) <= 0)
        {
            if (SSL_get_verify_result(ssl) != X509_V_OK)
            {
                printf("Client certificate verify error\n");
                printf("Connection close\n");
            }
            else
            {
                printf("Other connection error\n");
                printf("Connection close\n");
            }
        }
        else
        {
            printf("get connect!!\n");
            printf("Verification client success!!\n");
            ShowCerts(ssl);        /* get any certificates */

            count = SSL_read(ssl, receive, sizeof(receive));
            receive[count] = 0;
            printf("Received from client:\n");
            printf("%s\n\n", receive);

            if (strcmp(receive, "list_file") == 0)
            {
                reply = "choise a file to copy\n";
                SSL_write(ssl, reply, strlen(reply));   // 送出訊息
                if ((fp = popen("ls | cat", "r")) == NULL)
                {
                    perror("open failed!");
                    return -1;
                }
                char buf[256];
                while (fgets(buf, 255, fp) != NULL)
                {
                    SSL_write(ssl, buf, strlen(buf));
                }
                printf("ls done\n");
                if (pclose(fp) == -1)
                {
                    perror("close failed!");
                    return -2;
                }
            }
            else if (strncmp(receive, "copy_file", 9) == 0)
            {
                char *file_name = strtok(receive, " ");
                file_name = strtok(NULL, " ");

                if ((fp = fopen(file_name, "rb")) == NULL)
                {
                    perror("File opening failed");
                    return -1;
                }
                printf("Copying file: %s ... ...\n", file_name);
                char r[64] = "Copying_file ";
                strcat(r, file_name);
                SSL_write(ssl, r, strlen(r));   // write state to client
                fseek(fp, 0, SEEK_END);
                int file_size = ftell(fp);
                fseek(fp, 0, SEEK_SET);
                unsigned char *c = malloc(file_size * sizeof(char));
                fread(c, file_size, 1, fp);
                SSL_write(ssl, c, file_size);   // write whole file to client
                printf("File copy complete\n");
                fclose(fp);
                free(c);
            }
        }

        SSL_shutdown(ssl);
        SSL_free(ssl);
        close(client);
    }

    close(sock);
    SSL_CTX_free(ctx);
}