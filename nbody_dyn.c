#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include "timer.h"

#define G 6.67430e-11

//общие данные
int n, nthreads, stride, chunk;
long steps;
double dt;

double *m;
double *x, *y, *z;
double *vx, *vy, *vz;
double *fx, *fy, *fz;      //силы

atomic_int next_row;       //общий счётчик следующей строки

pthread_barrier_t barrier;
FILE *out;

void write_row(double t) {
    fprintf(out, "%.9g", t);
    for (int i = 0; i < n; i++)
        fprintf(out, ",%.9g,%.9g", x[i], y[i]);
    fprintf(out, "\n");
}

void *worker(void *arg) {
    intptr_t rank = (intptr_t)arg;

    double *mfx = fx + rank * n;
    double *mfy = fy + rank * n;
    double *mfz = fz + rank * n;

    int lo = (int)(rank * n / nthreads);
    int hi = (int)((rank + 1) * n / nthreads);

    for (long step = 0; step < steps; step++) {

        //считаем силы
        for (int i = 0; i < n; i++) mfx[i] = mfy[i] = mfz[i] = 0.0;

        //берём строки из общего счётчика, пока они не кончатся. atomic_fetch_add выдаёт каждому потоку уникальный номер
        for (;;) {
            int start = atomic_fetch_add(&next_row, chunk); //возращает старое значение счётчика и прибавляет к нему chunk
            if (start >= n) break; //строки кончились
            int end = start + chunk;
            if (end > n) end = n;

            for (int i = start; i < end; i++) {
                for (int j = i + 1; j < n; j++) {
                    double dx = x[j] - x[i];
                    double dy = y[j] - y[i];
                    double dz = z[j] - z[i];
                    double d2 = dx*dx + dy*dy + dz*dz;

                    if (d2 < 1e-20) continue;
                    double d  = sqrt(d2);
                    if (d < 1e-12) continue;

                    double f  = G * m[i] * m[j] / (d2 * d);

                    mfx[i] += f * dx;  mfy[i] += f * dy;  mfz[i] += f * dz;
                    mfx[j] -= f * dx;  mfy[j] -= f * dy;  mfz[j] -= f * dz;
                }
            }
        }

        pthread_barrier_wait(&barrier);   //ждём, пока все досчитают силы

        /*все потоки уже вышли из этапа 1, счётчик больше никто не трогает.
           Сбрасываем его для следующего шага (барьер ниже гарантирует,
           что к началу следующего шага сброс виден всем). */
        if (rank == 0) atomic_store(&next_row, 0); //заново раздаём строки

        //суммируем силы и двигаем тела
        for (int i = lo; i < hi; i++) {
            double Fx = 0, Fy = 0, Fz = 0;
            for (int t = 0; t < nthreads; t++) {
                Fx += fx[t * n + i];
                Fy += fy[t * n + i];
                Fz += fz[t * n + i];
            }
            if (m[i] > 1e-12) {    //защита от деления на 0 при обновлении скоростей
                x[i] += vx[i] * dt;
                y[i] += vy[i] * dt;
                z[i] += vz[i] * dt;
                vx[i] += Fx / m[i] * dt; //ускорение умножить на дельта t
                vy[i] += Fy / m[i] * dt;
                vz[i] += Fz / m[i] * dt;
            }
        }

        pthread_barrier_wait(&barrier);   //все подвинули тела

        if (rank == 0 && (step + 1) % stride == 0)
            write_row((step + 1) * dt);
    }
    return NULL;
}

int main(int argc, char *argv[]) {
    if (argc < 3) {
        fprintf(stderr, "Использование: %s tend input.txt [dt] [nthreads] [stride] [chunk]\n", argv[0]);
        return 1;
    }
    double tend = atof(argv[1]);
    const char *infile = argv[2];
    dt       = (argc > 3) ? atof(argv[3]) : 0.01;
    nthreads = (argc > 4) ? atoi(argv[4]) : 4;
    stride   = (argc > 5) ? atoi(argv[5]) : 1;
    chunk    = (argc > 6) ? atoi(argv[6]) : 1;
    if (dt <= 0 || nthreads < 1 || stride < 1 || chunk < 1) {
        fprintf(stderr, "Неверные параметры\n");
        return 1;
    }

    FILE *in = fopen(infile, "r");
    if (!in) { perror("input"); return 1; }
    if (fscanf(in, "%d", &n) != 1 || n < 1) { 
        fprintf(stderr, "Плохой файл\n");
        fclose(in);
        return 1;
    }

    m  = malloc(n * sizeof(double));
    x  = malloc(n * sizeof(double));  y  = malloc(n * sizeof(double));  z  = malloc(n * sizeof(double));
    vx = malloc(n * sizeof(double));  vy = malloc(n * sizeof(double));  vz = malloc(n * sizeof(double));
    for (int i = 0; i < n; i++) {
        if (fscanf(in, "%lf %lf %lf %lf %lf %lf %lf",
                   &m[i], &x[i], &y[i], &z[i], &vx[i], &vy[i], &vz[i]) != 7) {
            fprintf(stderr, "Ошибка чтения строки %d\n", i + 1);
            fclose(in);
            return 1;
        }
    }
    fclose(in);

    if (nthreads > n) nthreads = n;
    steps = (long)ceil(tend / dt);

    fx = calloc((size_t)nthreads * n, sizeof(double));
    fy = calloc((size_t)nthreads * n, sizeof(double));
    fz = calloc((size_t)nthreads * n, sizeof(double));
    atomic_init(&next_row, 0);

    out = fopen("out.csv", "w");
    if (!out) { perror("out.csv"); return 1; }
    write_row(0.0);

    int ret = pthread_barrier_init(&barrier, NULL, nthreads);
    if (ret != 0) {
        perror("pthread_barrier_init");
        return 1;
    }

    pthread_t *th = malloc(nthreads * sizeof(pthread_t));

    double t0, t1;
    GET_TIME(t0);
    for (int r = 0; r < nthreads; r++) {
        ret = pthread_create(&th[r], NULL, worker, (void *)(intptr_t)r);
        if (ret != 0) {
            fprintf(stderr, "pthread_create error %d\n", ret);
        }
    }
    for (int r = 0; r < nthreads; r++) {
        ret = pthread_join(th[r], NULL);
        if (ret != 0) {
            fprintf(stderr, "pthread_join error %d\n", ret);
        }
    }
    
    GET_TIME(t1);

    fprintf(stderr, "n=%d, потоков=%d, шагов=%ld, chunk=%d, время=%.6f с\n",
            n, nthreads, steps, chunk, t1 - t0);
            
    fclose(out);
    pthread_barrier_destroy(&barrier);
    free(th);
    free(m);
    free(x); free(y); free(z);
    free(vx); free(vy); free(vz);
    free(fx); free(fy); free(fz);
    
    return 0;
}
