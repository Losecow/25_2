#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <string.h>

int main()
{
  int number[10][6] = {0};
  int seed = 0;
  char alp[26] = "QWERTYUIOPASDFGHJKLZXCVBNM";

  char alp_num[10][3] = {""};
  int alp_r = 0;
  scanf("%d", &seed);
  srand(seed);

  // 문자만들기
  for (int j = 0; j < 10; j++)
  {
    for (int i = 0; i < 3; i++)
    {
      alp_r = rand() % 26 + 'a';

      printf("%d 번째 rand 값 입니다. : %d \n", i, alp_r);
      alp_num[j][i] = alp[alp_r];
      printf("해당문자: %c \n", alp_r);
    }
  }

  // 숫자만들기
  for (int j = 0; j < 10; j++)
  {
    for (int i = 0; i < 6; i++)
    {

      number[j][i] = rand() % 9;
      printf("%d 번째 rand 값 입니다. : %d \n", i, number[j][i]);

      if (number[j][0] == 0)
      {
        printf("제외되었습니다.\n");
        i--;
      }
    }
  }

  printf("10 codes have been generated.\n");

  for (int j = 0; j < 10; j++)
  {

    printf("Code #%d : ", j + 1);

    for (int i = 0; i < 3; i++)
    {
      printf("%c", alp_num[j][i]);
    }

    printf("-");

    for (int i = 0; i < 6; i++)
    {
      printf("%d", number[j][i]);
    }

    printf("\n");
  }
  return 0;
}