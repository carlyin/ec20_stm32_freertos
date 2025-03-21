#include "usbh_ec20.h"
#ifdef EC20_ATC_INTERFACE

typedef enum {
    SIM_ABSENT = 0,
    SIM_NOT_READY = 1,
    SIM_READY = 2, /* SIM_READY means the radio state is RADIO_STATE_SIM_READY */
    SIM_PIN = 3,
    SIM_PUK = 4,
    SIM_NETWORK_PERSONALIZATION = 5,
    SIM_BAD = 6,
} SIM_Status;

SIM_Status atc_sim_stat;
uint8_t atc_cgreg_stat = 0;
uint8_t atc_qnetdevctl_state = 0;
uint8_t atc_cgact_state = 0;
#define atc_qnetdectl_cid 1

#define MAX_AT_RESPONSE 2048
#define SendUrbBufLen 128
#define RecvUrbBufLen 512

struct atc_ctx {
    SemaphoreHandle_t sendSemaphoreId;
    uint8_t sendUrbBuf[SendUrbBufLen];
    struct urb sendUrb;

    SemaphoreHandle_t recvSemaphoreId;
    uint8_t recvUrbBuf[RecvUrbBufLen+1];
    struct urb recvUrb;

    char ATBuffer[MAX_AT_RESPONSE];
    uint32_t ATBufferLength;
    char *finalResponse;
};

static struct atc_ctx atc_ctx[1];

static const char * s_finalResponsesError[] = {
    "ERROR",
    "+CMS ERROR:",
    "+CME ERROR:",
    "NO CARRIER", /* sometimes! */
    "NO ANSWER",
    "NO DIALTONE",
};

static const char * s_finalResponsesSuccess[] = {
    "OK",
    "CONNECT"       /* some stacks start up data on another channel */
};

#define NUM_ELEMS(x) (sizeof(x)/sizeof(x[0]))

/** returns 1 if line starts with prefix, 0 if it does not */
static int strStartsWith(const char *line, const char *prefix)
{
    for ( ; *line != '\0' && *prefix != '\0' ; line++, prefix++) {
        if (*line != *prefix) {
            return 0;
        }
    }

    return *prefix == '\0';
}

static int isspace(char ch)
{
    return ch == ' ';
}

static char * strsep(char **stringp, const char *delim)
{
    char *s;
    const char *spanp;
    int c, sc;
    char *tok;

    if ((s = *stringp) == NULL)
        return (NULL);
    for (tok = s;;) {
        c = *s++;
        spanp = delim;
        do {
            if ((sc = *spanp++) == c) {
                if (c == 0)
                    s = NULL;
                else
                    s[-1] = 0;
                *stringp = s;
                return (tok);
            }
        } while (sc != 0);
    }
    /* NOTREACHED */
}

static int isFinalResponseError(const char *line)
{
    int i;

    for (i = 0 ; i < NUM_ELEMS(s_finalResponsesError) ; i++) {
        if (strStartsWith(line, s_finalResponsesError[i])) {
            return 1;
        }
    }

    return 0;
}

static int isFinalResponseSuccess(const char *line)
{
    int i;

    for (i = 0 ; i < NUM_ELEMS(s_finalResponsesSuccess) ; i++) {
        if (strStartsWith(line, s_finalResponsesSuccess[i])) {
            return 1;
        }
    }

    return 0;
}

/**
 * returns 1 if line is a final response, either  error or success
 * See 27.007 annex B
 * WARNING: NO CARRIER and others are sometimes unsolicited
 */
static int isFinalResponse(const char *line)
{
    return isFinalResponseSuccess(line) || isFinalResponseError(line);
}

static int at_tok_start(char **p_cur)
{
    if (*p_cur == NULL) {
        return -1;
    }

    // skip prefix
    // consume "^[^:]:"

    *p_cur = strchr(*p_cur, ':');

    if (*p_cur == NULL) {
        return -1;
    }

    (*p_cur)++;

    return 0;
}

static void skipWhiteSpace(char **p_cur)
{
    if (*p_cur == NULL) return;

    while (**p_cur != '\0' && isspace(**p_cur)) {
        (*p_cur)++;
    }
}

static void skipNextComma(char **p_cur)
{
    if (*p_cur == NULL) return;

    while (**p_cur != '\0' && **p_cur != ',') {
        (*p_cur)++;
    }

    if (**p_cur == ',') {
        (*p_cur)++;
    }
}

static char * nextTok(char **p_cur)
{
    char *ret = NULL;

    skipWhiteSpace(p_cur);

    if (*p_cur == NULL) {
        ret = NULL;
    } else if (**p_cur == '"') {
        (*p_cur)++;
        ret = strsep(p_cur, "\"");
        skipNextComma(p_cur);
    } else {
        ret = strsep(p_cur, ",");
    }

    return ret;
}

static int at_tok_nextint_base(char **p_cur, int *p_out, int base, int  uns)
{
    char *ret;

    if (*p_cur == NULL) {
        return -1;
    }

    ret = nextTok(p_cur);

    if (ret == NULL) {
        return -1;
    } else {
        long l;
        char *end;

        if (uns)
            l = strtoul(ret, &end, base);
        else
            l = strtol(ret, &end, base);

        *p_out = (int)l;

        if (end == ret) {
            return -1;
        }
    }

    return 0;
}

/**
 * Parses the next base 10 integer in the AT response line
 * and places it in *p_out
 * returns 0 on success and -1 on fail
 * updates *p_cur
 */
static int at_tok_nextint(char **p_cur, int *p_out)
{
    return at_tok_nextint_base(p_cur, p_out, 10, 0);
}

/**
 * Parses the next base 16 integer in the AT response line
 * and places it in *p_out
 * returns 0 on success and -1 on fail
 * updates *p_cur
 */
static int at_tok_nexthexint(char **p_cur, int *p_out)
{
    return at_tok_nextint_base(p_cur, p_out, 16, 1);
}

static int at_tok_nextstr(char **p_cur, char **p_out)
{
    if (*p_cur == NULL) {
        return -1;
    }

    *p_out = nextTok(p_cur);

    return 0;
}

/**
 * Returns a pointer to the end of the next line
 * special-cases the "> " SMS prompt
 *
 * returns NULL if there is no complete line
 */
static char * findNextEOL(char *cur)
{
    if (cur[0] == '>' && cur[1] == ' ' && cur[2] == '\0') {
        /* SMS prompt character...not \r terminated */
        return cur+2;
    }

    // Find next newline
    while (*cur != '\0' && *cur != '\r' && *cur != '\n') cur++;

    return *cur == '\0' ? NULL : cur;
}

static void atc_parse(struct atc_ctx *ctx, char *line) {
    char *p_eol;

    while (1) {
        int err;
        // skip over leading newlines
        while (*line == '\r' || *line == '\n')
            line++;

        p_eol = findNextEOL(line);
        if (p_eol == NULL)
            break;
        *p_eol = '\0';
        printf("AT< %.*s\n", p_eol - line, line);

        if (isFinalResponse(line)) {
            ctx->finalResponse = line;
            break;
        }
        else if (strStartsWith(line, "+CGREG: ") || strStartsWith(line, "+C5GREG: "))
        {
            /* +CGREG: <n>, <stat>, <lac>, <cid>, <networkType> */
            int n, stat, lac, cid, networkType;
            err = at_tok_start(&line);
            if (err < 0) goto error;
            err = at_tok_nextint(&line, &n);
            if (err < 0) goto error;
            err = at_tok_nextint(&line, &stat);
            if (err < 0) goto error;
            atc_cgreg_stat = stat;
            err = at_tok_nexthexint(&line, &lac);
            if (err < 0) goto error;
            err = at_tok_nexthexint(&line, &cid);
            if (err < 0) goto error;
            err = at_tok_nextint(&line, &networkType);
            if (err < 0) goto error;
            printf("stat = %d, lac = 0x%x, cid = 0x%x, networkType = %d\n",  stat, lac, cid, networkType);
        }
        else if (strStartsWith(line, "+COPS: "))
        {
            //+COPS:<mode>[,<format>[,<oper>][,<Act>]]
            int mode, format, act;
            char *operator;
            err = at_tok_start(&line);
            if (err < 0) goto error;
            err = at_tok_nextint(&line, &mode);
            if (err < 0) goto error;
            err = at_tok_nextint(&line, &format);
            if (err < 0) goto error;
            err = at_tok_nextstr(&line, &operator);
            if (err < 0) goto error;
            err = at_tok_nextint(&line, &act);
            printf("operator = %s, act = %d\n", operator, act);
        }
        else if (strStartsWith(line, "+CPIN: "))
        {
            char *cpinResult;
            err = at_tok_start(&line);
            if (err < 0) goto error;
            err = at_tok_nextstr(&line, &cpinResult);
            if (err < 0) goto error;
            printf("cpin = %s\n", cpinResult);
            if (0 == strcmp (cpinResult, "SIM PIN")) {
                atc_sim_stat = SIM_PIN;
            }
            else if (0 == strcmp (cpinResult, "SIM PUK")) {
                atc_sim_stat = SIM_PUK;
            }
            else if (0 == strcmp (cpinResult, "PH-NET PIN")) {
                atc_sim_stat = SIM_NETWORK_PERSONALIZATION;
            }
            else if (0 == strcmp (cpinResult, "READY")) {
                atc_sim_stat = SIM_READY;
            }
            else {
                atc_sim_stat = SIM_ABSENT;
            }
        }
        else if (strStartsWith(line, "+CSQ: "))
        {
            int signalStrength;  /* Valid values are (0-31, 99) as defined in TS 27.007 8.5 */
            int bitErrorRate;    /* bit error rate (0-7, 99) as defined in TS 27.007 8.5 */
            err = at_tok_start(&line);
            if (err < 0) goto error;
            err = at_tok_nextint(&line, &signalStrength);
            if (err < 0) goto error;
            err = at_tok_nextint(&line, &bitErrorRate);
            if (err < 0) goto error;
            printf("signalStrength = %d, bitErrorRate = %d\n", signalStrength, bitErrorRate);
        }
        else if (strStartsWith(line, "+QNETDEVCTL: "))
        {
            //+QNETDECTL:<op>,<cid>,<urc_en>,<state>
            int bind, cid, urc_en, state;
            err = at_tok_start(&line);
            if (err < 0) goto error;
            err = at_tok_nextint(&line, &bind);
            if (err < 0) goto error;
            err = at_tok_nextint(&line, &cid);
            if (err < 0) goto error;
            err = at_tok_nextint(&line, &urc_en);
            if (err < 0) goto error;
            err = at_tok_nextint(&line, &state);
            if (err < 0) goto error;
            if (cid == atc_qnetdectl_cid || cid == 0) {
                atc_qnetdevctl_state = state;
            }
        }
        else if (strStartsWith(line, "+CGACT: "))
        {
            //+CGACT:<cid>,<state>
            int cid, state;
            err = at_tok_start(&line);
            if (err < 0) goto error;
            err = at_tok_nextint(&line, &cid);
            if (err < 0) goto error;
            err = at_tok_nextint(&line, &state);
            if (err < 0) goto error;
            if (cid == atc_qnetdectl_cid) {
                atc_cgact_state = state;
            }
        }
        else if (strStartsWith(line, "+CGACT: "))
        {
            //+CGACT:<cid>,<state>
            int cid, state;
            err = at_tok_start(&line);
            if (err < 0) goto error;
            err = at_tok_nextint(&line, &cid);
            if (err < 0) goto error;
            err = at_tok_nextint(&line, &state);
            if (err < 0) goto error;
            if (cid == atc_qnetdectl_cid) {
                atc_cgact_state = state;
            }
        }

error:
        line = p_eol + 1;
    }

    return;
}

static void atc_recv_urb_complete(struct urb *urb, int status) {
    struct atc_ctx *ctx = (struct atc_ctx *)urb->ctx;
    char *p_eol, *line;

    if (status != 0) {
        printf("%s status=%d\n", __func__, status);
        return;
    }

    if (urb->actual_length == 0) {
        usb_submit_urb(urb);
        return;
   }

    if ((ctx->ATBufferLength + urb->actual_length+ 1) > MAX_AT_RESPONSE) {
        printf("%s overrun! %u/%u\n", __func__, ctx->ATBufferLength, urb->actual_length);
        ctx->ATBufferLength = 0;
    }
    urb->transfer_buffer[urb->actual_length] = 0;

    if (ctx->sendUrbBuf[0]) {
        memcpy(ctx->ATBuffer + ctx->ATBufferLength, urb->transfer_buffer, urb->actual_length+ 1);
        ctx->ATBufferLength += urb->actual_length;
    }

    line = (char *)urb->transfer_buffer;
    while (1) {
        // skip over leading newlines
        while (*line == '\r' || *line == '\n')
            line++;

        p_eol = findNextEOL(line);
        if (p_eol == NULL)
            break;

        if (ctx->sendUrbBuf[0]) {
            if (isFinalResponse(line)) {
                xSemaphoreGive(ctx->recvSemaphoreId);
            }
        }
        else {
            printf("AT< %.*s\n", p_eol - line, line);
            //URC
        }

        line = p_eol + 1;
    }

    usb_submit_urb(urb);
}

static void atc_send_urb_complete(struct urb *urb, int status) {
    struct atc_ctx *ctx = (struct atc_ctx *)urb->ctx;

    if (status != 0) {
        printf("%s status=%d\n", __func__, status);
        return;
    }

    xSemaphoreGive(ctx->sendSemaphoreId);
}

int at_send_command (const char *command)
{
    struct atc_ctx *ctx = &atc_ctx[0];
    int len = strlen(command);

    printf("AT> %s\n", command);

    memcpy(ctx->sendUrbBuf, command, len);
    ctx->sendUrbBuf[len++] = '\r';

    while (xSemaphoreTake(ctx->sendSemaphoreId, msec_to_ticks(0)) == pdTRUE);
    while (xSemaphoreTake(ctx->recvSemaphoreId, msec_to_ticks(0)) == pdTRUE);

    ctx->finalResponse = NULL;
    ctx->ATBufferLength = 0;
    ctx->sendUrb.transfer_buffer_length = len;
    if (usb_submit_urb(&ctx->sendUrb) != 0 ) {
        return -1;
    }

    if (xSemaphoreTake(ctx->sendSemaphoreId, msec_to_ticks(3000)) != pdTRUE) {
        printf("%s send timeout\n", __func__);
        return -2;
    }

    if (xSemaphoreTake(ctx->recvSemaphoreId, msec_to_ticks(5000)) != pdTRUE) {
        printf("%s recv timeout\n", __func__);
        return -3;
    }

    ctx->sendUrbBuf[0] = '\0';
    if (ctx->ATBufferLength > 0)
        atc_parse(ctx, ctx->ATBuffer);
    ctx->ATBufferLength = 0;

    return ctx->finalResponse && isFinalResponseSuccess(ctx->finalResponse);
}

static int atc_open(struct hc_info *hc_info, uint16_t idProduct) {
    struct atc_ctx *ctx = &atc_ctx[0];
    struct urb *urb;

    printf("%s hc_num=%u,%u\n", __func__,
        get_hc_num(HC_BULK_OUT), get_hc_num(HC_BULK_IN));

    urb = &ctx->sendUrb;
    urb ->hc_type = HC_BULK_OUT;
    urb ->hc_num = get_hc_num(urb ->hc_type);
    urb ->ep_size = get_ep_size(urb ->hc_type);
    urb ->urb_state = URB_STATE_IDLE;
    urb ->transfer_buffer = ctx->sendUrbBuf;
    urb ->complete = atc_send_urb_complete;
    urb ->ctx = (void *)ctx;
    usb_start_urb(urb);

    urb = &ctx->recvUrb;
    urb ->hc_type = HC_BULK_IN;
    urb ->hc_num = get_hc_num(urb ->hc_type);
    urb ->ep_size = get_ep_size(urb ->hc_type);
    urb ->urb_state = URB_STATE_IDLE;
    urb ->transfer_buffer = ctx->recvUrbBuf;
    urb ->transfer_buffer_length = RecvUrbBufLen;
    urb ->complete = atc_recv_urb_complete;
    urb ->ctx = (void *)ctx;
    usb_start_urb(urb);

    return usb_submit_urb(urb);;
}

static const struct usb_driver atc_dirver = {
    EC20_ATC_INTERFACE,0xFF,
    hc_mask_bulk_in_out,
    atc_open,
    NULL
};

void atc_init(void) {
    struct atc_ctx *ctx = &atc_ctx[0];

    vSemaphoreCreateBinary(ctx->sendSemaphoreId);
    vSemaphoreCreateBinary(ctx->recvSemaphoreId);
    usb_register_drv(&atc_dirver);
}
#endif
