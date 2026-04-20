/* $begin tinymain */
/*
 * tiny.c - A simple, iterative HTTP/1.0 Web server that uses the
 *     GET method to serve static and dynamic content.
 *
 * Updated 11/2019 droh
 *   - Fixed sprintf() aliasing issue in serve_static(), and clienterror().
 */
#include "csapp.h"

void doit(int fd);
void read_requesthdrs(rio_t *rp);
int parse_uri(char *uri, char *filename, char *cgiargs);
void serve_static(int fd, char *filename, int filesize);
void get_filetype(char *filename, char *filetype);
void serve_dynamic(int fd, char *filename, char *cgiargs);
void clienterror(int fd, char *cause, char *errnum, char *shortmsg,
                 char *longmsg);

int main(int argc, char **argv)
{
  int listenfd, connfd;
  char hostname[MAXLINE], port[MAXLINE];
  socklen_t clientlen;
  struct sockaddr_storage clientaddr;

  /* Check command line args */
  if (argc != 2)
  {
    fprintf(stderr, "usage: %s <port>\n", argv[0]);
    exit(1);
  }

  listenfd = Open_listenfd(argv[1]);
  while (1)
  {
    clientlen = sizeof(clientaddr);
    connfd = Accept(listenfd, (SA *)&clientaddr,
                    &clientlen); // line:netp:tiny:accept
    Getnameinfo((SA *)&clientaddr, clientlen, hostname, MAXLINE, port, MAXLINE,
                0);
    /*
      여기까지의 과정은 echo Server 에서의 흐름과 동일하다.
      getaddrinfo -> socket -> bind -> listen -> accept
      echo와 달라진 점은 text data가 아닌 HTTP 요청을 받기 때문에
      그 처리를 doit() 함수에서 일괄적으로 처리한다.
    */
    printf("Accepted connection from (%s, %s)\n", hostname, port);
    doit(connfd);  // line:netp:tiny:doit
    Close(connfd); // line:netp:tiny:close
  }
}

// HTTP 요청을 읽고, 정적 콘텐츠인지 동적 콘텐츠인지 검사하고, 응답을 보내는 함수
void doit(int fd)
{
  /*
    1. HTTP 요청을 읽는다.
    2. 요청 method를 검사한다.(GET이 아니면 error)
    3. Header를 읽는다.
    4. URI 파싱 + 파일 검사(일반? 읽기 전용? 실행?)
    5. 검사 한 파일의 타입(정적/동적)에 따라서 분기 처리
  */

  int is_static;
  struct stat sbuf;
  char buf[MAXLINE], method[MAXLINE], uri[MAXLINE], version[MAXLINE];
  char filename[MAXLINE], cgiargs[MAXLINE];
  rio_t rio;

  /* Read request line and headers */
  // HTTP 요청을 읽는다.
  Rio_readinitb(&rio, fd);
  Rio_readlineb(&rio, buf, MAXLINE);
  printf("Request headers:\n");
  printf("%s", buf);
  sscanf(buf, "%s %s %s", method, uri, version);

  // 요청 method 를 검사한다.
  if (strcasecmp(method, "GET"))
  {
    clienterror(fd, method, "501", "Not implemented",
                "Tiny does not implement this method");
    return;
  }

  // 요청 Header를 읽는다.
  read_requesthdrs(&rio);

  /* Parse URI from GET request */
  // URI를 파싱한다.
  is_static = parse_uri(uri, filename, cgiargs);

  // stat 반환값이 음수 -> stat() 함수가 실패했다.
  // 해당 파일이 없거나, 잘못된 경로, 접근 권한이 없음 등
  if (stat(filename, &sbuf) < 0)
  {
    clienterror(fd, filename, "404", "Not found",
                "Tiny couldn’t find this file");
    return;
  }

  // 정적 컨텐츠라면 is_static 변수에는 1이 들어가 있다.
  if (is_static)
  {
    // 일반 파일이 아니거나, 소유자 읽기 권한이 없는 경우
    if (!(S_ISREG(sbuf.st_mode)) || !(S_IRUSR & sbuf.st_mode))
    {
      clienterror(fd, filename, "403", "Forbidden",
                  "Tiny couldn’t read the file");
      return;
    }
    // static 처리하는 함수 호출
    serve_static(fd, filename, sbuf.st_size);
  }
  // 동적 컨텐츠 분기
  else
  { /* Serve dynamic content */
    if (!(S_ISREG(sbuf.st_mode)) || !(S_IXUSR & sbuf.st_mode))
    {
      clienterror(fd, filename, "403", "Forbidden",
                  "Tiny couldn’t run the CGI program");
      return;
    }
    // dynamic content 를 처리하는 함수 호출
    serve_dynamic(fd, filename, cgiargs);
  }
}