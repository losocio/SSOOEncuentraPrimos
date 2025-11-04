#include <stdio.h>
# include <time.h>

#define BASE 200000000
#define RANGO 2000
int Comprobarsiesprimo(long int numero);

int main(void) {
  long int numero;



  clock_t start, end;
  double cpu_time_used;
     
  start = clock();
  
  for (numero=BASE;numero<BASE+RANGO;numero++)
  {
	 if (Comprobarsiesprimo(numero))
		printf("%d \n", numero);
	  
  }

  end = clock();
  cpu_time_used = ((double) (end - start)) / CLOCKS_PER_SEC;
  printf("Tiempo %.0f (segundos)",cpu_time_used);


  return 0;
}

int Comprobarsiesprimo(long int numero) {
  if (numero < 2) return 0; // Por convenio 0 y 1 no son primos ni compuestos
  else
	for (int x = 2; x <= (numero / 2) ; x++)
		if (numero % x == 0) return 0;
  return 1;
}
