#include "trader/kdbp_db.h"

#include <iostream>
#include <thread>
#include <regex>
#include <string>
#include <chrono>

using namespace std;

I isRemoteErr(K x)
{
    if (!x)
    {
        fprintf(stderr, "Network error: %s\n", strerror(errno));
        return 1;
    }
    else if (-128 == xt)
    {
        fprintf(stderr, "Error message returned : %s\\n", x->s);
        r0(x);
        return 1;
    }
    return 0;
}

J Kdbp::castTime(struct tm *x)
{
    return (J)((60 * x->tm_hour + x->tm_min) * 60 + x->tm_sec) * 1000000000;
}

int Kdbp::insertMultRow(const string &query, const string &table, K rows)
{
    K result;

    //sync
    result = k(this->_kdb, S(query.c_str()), ks((S)table.c_str()), rows, (K)0);
    //async
    // result = k(-this->_kdb, query, (K)0);

    if (isRemoteErr(result))
    {
        return 1;
    }
    // result->n - length;
    return 0;
}

int Kdbp::insertRow(const string &query, const string &table, K row)
{
    K result;

    //sync
    result = k(this->_kdb, (S)query.c_str(), ks((S)table.c_str()), row, (K)0);
    //async
    // result = k(-this->_kdb, query, (K)0);

    if (isRemoteErr(result))
    {
        return 1;
    }
    // result->n - length;
    return 0;
}

int Kdbp::deleteRow(const string &query, const string &table, K row)
{
    K result;

    //sync
    result = k(this->_kdb, (S)query.c_str(), ks((S)table.c_str()), row, (K)0);
    //async
    // result = k(-this->_kdb, query, (K)0);

    if (isRemoteErr(result))
    {
        return 1;
    }
    // result->n - length;
    return 0;
}

int Kdbp::executeQuery(const string &query)
{
    K result;

    //sync
    result = k(this->_kdb, (S)query.c_str(), (K)0);
    //async
    // result = k(-this->_kdb, query, (K)0);

    if (isRemoteErr(result))
    {
        return 1;
    }
    // result->n - length;
    return 0;
}

K Kdbp::readQuery(const string &query)
{
    K result;

    //sync
    result = k(this->_kdb, (S)query.c_str(), (K)0);
    //async
    // result = k(-this->_kdb, query, (K)0);

    if (isRemoteErr(result))
    {
        return (K)0;
    }
    // result->n - length;
    return result;
}

void Kdbp::fmt_time(char *str, time_t time, int adjusted)
{
    static char buffer[4096];

    struct tm *timeinfo = localtime(&time);
    if (adjusted)
        timeinfo->tm_hour -= 1;
    strftime(buffer, sizeof(buffer), str, timeinfo);

    printf("%s ", buffer);
}

K Kdbp::printatom(K x)
{
    char *p = new char[100];
    switch (xt)
    {
    case -1:
        printf("%db", x->g);
        break;
    case -4:
        printf("0x%02x", x->g);
        break;
    case -5:
        printf("%d", x->h);
        break;
    case -6:
        printf("%d", x->i);
        break;
    case -7:
        printf("%lld", x->j);
        break;
    case -8:
        printf("%.2f", x->e);
        break;
    case -9:
        printf("%.2f", x->f);
        break;
    case -10:
        printf("\"%c\"", x->g);
        break;
    case -11:
        printf("`%s", x->s);
        break;
    case -12:
        strcpy(p, "%Y.%m.%dD%H:%M:%S.");
        fmt_time(p, ((x->j) / 8.64e13 + 10957) * 8.64e4, 0);
        break;
    case -13:
        printf("%04d.%02d", (x->i) / 12 + 2000, (x->i) % 12 + 1);
        break;
    case -14:
        strcpy(p, "%Y.%m.%d");
        fmt_time(p, ((x->i) + 10957) * 8.64e4, 0);
        break;
    case -15:
        strcpy(p, "%Y.%m.%dD%H:%M:%S");
        fmt_time(p, ((x->f) + 10957) * 8.64e4, 0);
        break;
    case -16:
    {
        strcpy(p, "%jD%H:%M:%S");
        fmt_time(p, (x->j) / 1000000000, 1);
        printf(".%09lld", (x->j) % 1000000000);
        break;
    }
    case -17:
        strcpy(p, "%H:%M");
        fmt_time(p, (x->i) * 60, 1);
        break;
    case -18:
        strcpy(p, "%H:%M:%S");
        fmt_time(p, x->i, 1);
        break;
    case -19:
    {
        strcpy(p, "%H:%M:%S");
        fmt_time(p, (x->i) / 1000, 1);
        printf(".%03d", (x->i) % 1000);
        break;
    }
    default:
        strcpy(p, "notimplemented");
        return krr(p);
    }

    return (K)0;
}

#define showatom(c, a, x, i) \
    do                       \
    {                        \
        K r = c(a((x))[i]);  \
        printatom(r);        \
        r0(r);               \
    } while (0)

K Kdbp::getitem(K x, int index)
{
    char *p = new char[100];
    K r;
    switch (xt)
    {
    case 0:
        r = kK(x)[index];
        break;
    case 1:
        r = kb(kG((x))[index]);
        break;
    case 4:
        r = kg(kG((x))[index]);
        break;
    case 5:
        r = kh(kH((x))[index]);
        break;
    case 6:
        r = ki(kI((x))[index]);
        break;
    case 7:
        r = kj(kJ((x))[index]);
        break;
    case 8:
        r = ke(kE((x))[index]);
        break;
    case 9:
        r = kf(kF((x))[index]);
        break;
    case 10:
        r = kc(kC((x))[index]);
        break;
    case 11:
        r = ks(kS((x))[index]);
        break;
    case 14:
        r = kd(kI((x))[index]);
        break;
    case 15:
        r = kz(kF((x))[index]);
        break;
    default:
        strcpy(p, "notimplemented");
        r = krr(p);
    }

    return r;
}

K Kdbp::printitem(K x, int index)
{
    char *p = new char[100];
    switch (xt)
    {
    case 0:
        printq(kK(x)[index]);
        break;
    case 1:
        showatom(kb, kG, x, index);
        break;
    case 4:
        showatom(kg, kG, x, index);
        break;
    case 5:
        showatom(kh, kH, x, index);
        break;
    case 6:
        showatom(ki, kI, x, index);
        break;
    case 7:
        showatom(kj, kJ, x, index);
        break;
    case 8:
        showatom(ke, kE, x, index);
        break;
    case 9:
        showatom(kf, kF, x, index);
        break;
    case 10:
        showatom(kc, kC, x, index);
        break;
    case 11:
        showatom(ks, kS, x, index);
        break;
    case 14:
        showatom(kd, kI, x, index);
        break;
    case 15:
        showatom(kz, kF, x, index);
        break;
    default:
        strcpy(p, "notimplemented");
        return krr(p);
    }

    return (K)0;
}

K Kdbp::printlist(K x)
{
    if (x->n == 1)
        printf(",");

    for (int i = 0; i < x->n; i++)
        printitem(x, i);

    return (K)0;
}

K Kdbp::printdict(K x)
{
    K keys = kK(x)[0];
    K data = kK(x)[1];

    for (int row = 0; row < keys->n; row++)
    {
        printitem(keys, row);
        printf("| ");
        printitem(data, row);
        if (row < keys->n - 1)
            printf("\n");
    }

    return (K)0;
}

K Kdbp::printtable(K x)
{
    K flip = ktd(x);
    K columns = kK(flip->k)[0];
    K rows = kK(flip->k)[1];

    int colcount = columns->n;
    int rowcount = kK(rows)[0]->n;

    for (int i = 0; i < colcount; i++)
        printf("%s\t", kS(columns)[i]);
    printf("\n");

    for (int i = 0; i < rowcount; i++)
    {
        for (int j = 0; j < colcount; j++)
        {
            printitem(kK(rows)[j], i);
            printf("\t");
        }
        printf("\n");
    }

    return (K)0;
}

K Kdbp::printq(K x)
{
    K result;
    char *p = new char[100];
    strcpy(p, "notimplemented");
    if (xt < 0)
        result = printatom(x);
    else if ((xt >= 0) && (xt < 20))
        result = printlist(x);
    else if (xt == 98)
        result = printtable(x);
    else if (xt == 99)
        result = printdict(x);
    else
        result = krr(p);

    printf("\n");
    return result;
}