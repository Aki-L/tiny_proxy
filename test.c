#include<stdio.h>
#include<string.h>

int main(){
	char msg1[1024];
	char msg2[1024];
	char msg3[1024];
	char msg4[1024];
	char *token;
	const char s[2] = ":";
	
	sscanf("aki: aki", "%s: %s", msg1, msg2);
	sscanf("aki: aki", "%[^:]: %s", msg3, msg4);
	printf("%s %s %s %s", msg1, msg2, msg3, msg4);
}
