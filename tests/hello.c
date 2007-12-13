
#include <libos/libos.h>

extern void init(void);

void start(void)
{

	init();

	printf("Hello World\n");
}
