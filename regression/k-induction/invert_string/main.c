#include <assert.h>

char nondet_char()
{
  char c;
  return c;
}

unsigned int nondet_uint()
{
  unsigned int ui;
  return ui;
}

int main() {
    unsigned int max = nondet_uint()%5;
    char str1[max], str2[max];
    int i, j;

    for (i=0; i<max; i++) {
        str1[i]=nondet_char();
    }

    str1[max-1]= '\0';

    j = 0;
   
    for (i = max - 1; i >= 0; i--) {
        str2[j] = str1[i];
        j++;
    }

    j = max-1;
    for (i=0; i<max; i++) {
      assert(str1[i] == str2[j]);
      j--;
    }   
}

