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

// Header를 읽는 함수
void read_requesthdrs(rio_t *rp)
{
  char buf[MAXLINE];
  // 개행문자를 기준으로 한줄씩 읽어 buf에 넣는다.
  Rio_readlineb(rp, buf, MAXLINE);
  // 빈 줄이 나올때까지 반복한다 -> Header의 끝은 빈 줄
  while (strcmp(buf, "\r\n"))
  {
    Rio_readlineb(rp, buf, MAXLINE);
    printf("%s", buf);
  }
  return;
}

void clienterror(int fd, char *cause, char *errnum, char *shortmsg,
                 char *longmsg)
{
  char buf[MAXLINE], body[MAXBUF];

  /* Build the HTTP response body */
  sprintf(body, "<html><title>Tiny Error</title>");
  sprintf(body, "%s<body bgcolor=\"ffffff\">\r\n", body);
  sprintf(body, "%s%s: %s\r\n", body, errnum, shortmsg);
  sprintf(body, "%s<p>%s: %s\r\n", body, longmsg, cause);
  sprintf(body, "%s<hr><em>The Tiny Web server</em>\r\n", body);

  /* Print the HTTP response */
  sprintf(buf, "HTTP/1.0 %s %s\r\n", errnum, shortmsg);
  Rio_writen(fd, buf, strlen(buf));
  sprintf(buf, "Content-type: text/html\r\n");
  Rio_writen(fd, buf, strlen(buf));
  sprintf(buf, "Content-length: %d\r\n\r\n", (int)strlen(body));
  Rio_writen(fd, buf, strlen(buf));
  Rio_writen(fd, body, strlen(body));
}

// uri를 파싱하는 함수
int parse_uri(char *uri, char *filename, char *cgiargs)
{
  char *ptr;

  // strstr() 함수 : 1번째 인자 -> 긴 문자열 전체, 2번째 인자 -> 그 안에서 찾고 싶은 문자열
  // 즉, uri에 "cgi-bin" 이 없다면 -> static 콘텐츠
  if (!strstr(uri, "cgi-bin"))
  {
    // 정적 컨텐츠이므로 CGI 인자가 필요없다.
    strcpy(cgiargs, "");
    // filename 에는 ./url ~ 이 들어간다.
    strcpy(filename, ".");
    strcat(filename, uri);

    // 마지막 글자가 '/' 임을 확인하는 조건문
    if (uri[strlen(uri) - 1] == '/')
      strcat(filename, "home.html");
    /*
    왜 위 조건문과 home.html을 붙이는 작업이 필요할까?
    예를 들어 사용자가 요청한 url이 http://localhost:8000/ 이라고 생각해보자.
    위 같은 경우에 뒤에 파일명을 안 쓰고 /만 요청하면 default 로 home.html을 보여주기 위한 장치.
    */
    return 1;
  }

  // 동적 콘텐츠인 경우
  else
  { /* Dynamic content */
    // ptr에 uri의 '?' 문자의 위치를 저장
    ptr = index(uri, '?');
    // 만약 ? 문자가 있다면 인자가 있는것이므로 뒤의 인자를 앞에 uri와 분리시켜줘야함.
    if (ptr)
    {
      // ptr 다음의 문자열을 cgiargs에 넣어주고
      strcpy(cgiargs, ptr + 1);
      // ptr이 가리키고 있는 자리의 문자를 문자열의 종료를 알려주는 문자 '\0'으로 교체함.
      *ptr = '\0';
    }

    // CGI 인자가 없는 경우에는 cgiargs 를 빈 문자열로.
    else
      strcpy(cgiargs, "");
    // 나머지 부분은 정적 콘텐츠 부분과 동일하다.
    strcpy(filename, ".");
    strcat(filename, uri);
    return 0;
  }
  // parse_uri 의 반환값은 정적 콘텐츠 일때는 1, 동적 콘텐츠 일때는 0을 반환한다.
}

// static 요청 처리 함수
void serve_static(int fd, char *filename, int filesize)
{
  int srcfd;
  char *srcp, filetype[MAXLINE], buf[MAXBUF];

  /* Send response headers to client */
  get_filetype(filename, filetype);
  sprintf(buf, "HTTP/1.0 200 OK\r\n");
  sprintf(buf, "%sServer: Tiny Web Server\r\n", buf);
  sprintf(buf, "%sConnection: close\r\n", buf);
  sprintf(buf, "%sContent-length: %d\r\n", buf, filesize);
  sprintf(buf, "%sContent-type: %s\r\n\r\n", buf, filetype);
  Rio_writen(fd, buf, strlen(buf));
  printf("Response headers:\n");
  printf("%s", buf);

  /* Send response body to client */
  srcfd = Open(filename, O_RDONLY, 0);
  srcp = Mmap(0, filesize, PROT_READ, MAP_PRIVATE, srcfd, 0);
  Close(srcfd);
  Rio_writen(fd, srcp, filesize);
  Munmap(srcp, filesize);
}

// dynamic 요청 처리 함수
void serve_dynamic(int fd, char *filename, char *cgiargs)
{
}