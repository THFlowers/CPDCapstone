#include <stdio.h>

unsigned long long fib(unsigned long long n)
{
	if (n <= 0) 
		return 0;
	if (n==1)
		return 1;
	return fib(n-1)+fib(n-2);
}

int
main(int argc, char* argv[])
{
	if (argc < 2)
		fprintf(stderr, "Usage: fib number\n");
	else
	{
		int num = atoi(argv[1]);
		printf("%ld\n", fib(num)); 
	}

	return 0;
}
