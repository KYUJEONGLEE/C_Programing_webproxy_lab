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
  int srcfd; // 보낼 파일을 연 파일 fd
  char *srcp, filetype[MAXLINE], buf[MAXBUF];
  /*
    srcp = mmap으로 파일을 메모리에 매핑한 시작 주소
    filetype = MIME 타입 저장용
    buf = HTTP 응답 헤더 문자열을 만들어 담는 버퍼
  */
  /*
    응답 헤더를 만든다.
  */
  get_filetype(filename, filetype); // 파일 타입을 결정하고 filetype에 넣는다.
  /*
    buf에 HTTP 응답 메시지의 헤더부분을 붙이는 과정
  */
  sprintf(buf, "HTTP/1.0 200 OK\r\n");
  sprintf(buf, "%sServer: Tiny Web Server\r\n", buf);
  sprintf(buf, "%sConnection: close\r\n", buf);
  sprintf(buf, "%sContent-length: %d\r\n", buf, filesize);
  sprintf(buf, "%sContent-type: %s\r\n\r\n", buf, filetype);

  // 여기서 인자로 받는 fd는 클라이언트와 연결된 소켓 fd
  // 즉, 위에서 만든 HTTP 응답 헤더를 브라우저에게 보낸다.
  Rio_writen(fd, buf, strlen(buf));

  // 서버 터미널에 출력
  printf("Response headers:\n");
  printf("%s", buf);

  /* 파일 내용을 실제로 보낸다 */
  srcfd = Open(filename, O_RDONLY, 0); // 진짜 보낼 파일을 연다(읽기 전용)
  /*
    파일 내용을 read로 따로 읽어와서 버퍼에 넣는 대신, 파일을 메모리 처럼 바로 다루게 하는것.
    즉, 파일 내용이 있는 곳을 메모리 주소 srcp 로 가리키게 만든다.
  */
  srcp = Mmap(0, filesize, PROT_READ, MAP_PRIVATE, srcfd, 0);
  Close(srcfd);

  // 여기서 파일 내용을 통째로 브라우저에게 보낸다.
  Rio_writen(fd, srcp, filesize);
  // 메모리 매핑을 해제한다.
  Munmap(srcp, filesize);
}

// dynamic 요청 처리 함수
void serve_dynamic(int fd, char *filename, char *cgiargs)
{
  // emptylist = Execve()에 넘길 argv 배열
  char buf[MAXLINE], *emptylist[] = {NULL};
  /*
    서버가 브라우저에게 먼저 전달하는 요소 2개
    요청 성공적으로 처리했다는 메시지 + 서버 이름
  */
  sprintf(buf, "HTTP/1.0 200 OK\r\n");
  Rio_writen(fd, buf, strlen(buf));
  sprintf(buf, "Server: Tiny Web Server\r\n");
  Rio_writen(fd, buf, strlen(buf));

  /*
    Fork()로 자식 프로세스 생성.
    자식이면 == 0
  */
  if (Fork() == 0)
  {
    setenv("QUERY_STRING", cgiargs, 1); // CGI 인자를 환경 변수로 설정한다.
    Dup2(fd, STDOUT_FILENO);            // 표준 출력을 클라이언트 소켓(브라우저)로 바꾼다.
    Execve(filename, emptylist, environ);
    /*
      원래 Tiny server의 자식 프로세스였지만, Execve() 후에는 해당 프로그램이 된다.
      execve()는 새 프로세스를 만드는게 아니라, 현재 프로세스 이미지를 새 프로그램으로 교치한다.
      즉, 여기서는 이 자식 프로세스는 CGI 코드(adder.c)를 실행하게 된다.
      위에서 설정한 dup2 덕분에 그 출력이 클라이언트로 가게됨.
    */
  }
  Wait(NULL); /* Parent waits for and reaps child */
  /*
    기다리는 이유
    - 자식이 동적 콘텐츠를 생성중인데, 부모가 너무 빨리 정리하거나 연결을 닫아버리면 안되기 때문
    - 좀비 프로세스 방지
  */
}

void get_filetype(char *filename, char *filetype)
{
  if (strstr(filename, ".html"))
    strcpy(filetype, "text/html");
  else if (strstr(filename, ".gif"))
    strcpy(filetype, "image/gif");
  else if (strstr(filename, ".png"))
    strcpy(filetype, "image/png");
  else if (strstr(filename, ".jpg"))
    strcpy(filetype, "image/jpeg");
  else
    strcpy(filetype, "text/plain");
}