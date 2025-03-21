#include "usbh_ec20.h"

#ifdef EC20_NMEA_INTERFACE
#include<math.h>

/** GpsLocation has valid latitude and longitude. */
#define GPS_LOCATION_HAS_LAT_LONG   0x0001
/** GpsLocation has valid altitude. */
#define GPS_LOCATION_HAS_ALTITUDE   0x0002
/** GpsLocation has valid speed. */
#define GPS_LOCATION_HAS_SPEED      0x0004
/** GpsLocation has valid bearing. */
#define GPS_LOCATION_HAS_BEARING    0x0008
/** GpsLocation has valid accuracy. */
#define GPS_LOCATION_HAS_ACCURACY   0x0010

typedef struct {
    const char*  p;
    const char*  end;
} Token;

//NOTE:the default value is 16,but the GSV messages's length is longer than 16,modified 16 to 20
#define  MAX_NMEA_TOKENS  20

typedef struct {
    int     count;
    Token   tokens[ MAX_NMEA_TOKENS ];
} NmeaTokenizer;

/** Represents a location. */
typedef struct {
    /** set to sizeof(GpsLocation) */
    size_t          size;
    /** Contains GpsLocationFlags bits. */
    uint16_t        flags;
    /** Represents latitude in degrees. */
    double          latitude;
    /** Represents longitude in degrees. */
    double          longitude;
    /** Represents altitude in meters above the WGS 84 reference
     * ellipsoid. */
    double          altitude;
    /** Represents speed in meters per second. */
    float           speed;
    /** Represents heading in degrees. */
    float           bearing;
    /** Represents expected accuracy in meters. */
    float           accuracy;
    /** Timestamp for the location fix. */
    //GpsUtcTime      timestamp;
} GpsLocation;

#define  NMEA_MAX_SIZE  83

typedef struct {
    int     pos;
    int     overflow;
    int     utc_year;
    int     utc_mon;
    int     utc_day;
    int     utc_diff;
    GpsLocation  fix;
    char    in[ NMEA_MAX_SIZE+1 ];
} NmeaReader;

#define RecvUrbBufLen 512
struct nmea_ctx {
    uint8_t recvUrbBuf[RecvUrbBufLen];
    struct urb recvUrb;

    NmeaReader  reader;
};

static struct nmea_ctx nmea_ctx[1];

static double str2float( const char*  p, const char*  end )
{
    //int   result = 0;
    int   len    = end - p;
    char  temp[16];

    if (len >= (int)sizeof(temp))
        return 0.;

    memcpy( temp, p, len );
    temp[len] = 0;
    return strtod( temp, NULL );
}

static void nmea_reader_init( NmeaReader*  r )
{
    memset( r, 0, sizeof(*r) );

    r->pos      = 0;
    r->overflow = 0;
    r->utc_year = -1;
    r->utc_mon  = -1;
    r->utc_day  = -1;
}

static int nmea_tokenizer_init( NmeaTokenizer*  t, const char*  p, const char*  end )
{
    int    count = 0;
    //char*  q;

    // the initial '$' is optional
    if (p < end && p[0] == '$')
        p += 1;

    // remove trailing newline
    if (end > p && end[-1] == '\n') {
        end -= 1;
        if (end > p && end[-1] == '\r')
            end -= 1;
    }

    // get rid of checksum at the end of the sentecne
    if (end >= p+3 && end[-3] == '*') {
        end -= 3;
    }

    while (p < end) {
        const char*  q = p;

        q = memchr(p, ',', end-p);
        if (q == NULL)
            q = end;
        //NOTE:q > p => q >= p
        //this is for BUG: blank tok could not be analyzed correct
        if (q >= p) {
            if (count < MAX_NMEA_TOKENS) {
                t->tokens[count].p   = p;
                t->tokens[count].end = q;
                count += 1;
            }
        }
        if (q < end)
            q += 1;

        p = q;
    }

    t->count = count;
    return count;
}

static Token nmea_tokenizer_get( NmeaTokenizer*  t, int  index )
{
    Token  tok;
    static const char*  dummy = "";

    if (index < 0 || index >= t->count) {
        tok.p = tok.end = dummy;
    } else
        tok = t->tokens[index];

    return tok;
}

static double convert_from_hhmm( Token  tok )
{
    double  val     = str2float(tok.p, tok.end);
    int     degrees = (int)(floor(val) / 100);
    double  minutes = val - degrees*100.;
    double  dcoord  = degrees + minutes / 60.0;
    return dcoord;
}

static int nmea_reader_update_latlong( NmeaReader* r,Token latitude,char latitudeHemi,Token longitude,char longitudeHemi )
{
    double   lat, lon;
    Token    tok;

    tok = latitude;
    if (tok.p + 6 > tok.end) {
        printf("latitude is too short: '%.*s'", tok.end-tok.p, tok.p);
        return -1;
    }
    lat = convert_from_hhmm(tok);
    if (latitudeHemi == 'S')
        lat = -lat;

    tok = longitude;
    if (tok.p + 6 > tok.end) {
        printf("longitude is too short: '%.*s'", tok.end-tok.p, tok.p);
        return -1;
    }
    lon = convert_from_hhmm(tok);
    if (longitudeHemi == 'W')
        lon = -lon;

    r->fix.flags    |= GPS_LOCATION_HAS_LAT_LONG;
    r->fix.latitude  = lat;
    r->fix.longitude = lon;
    return 0;
}

static int nmea_reader_update_bearing( NmeaReader*  r,
                            Token        bearing )
{
    Token   tok = bearing;

    if (tok.p >= tok.end)
        return -1;

    r->fix.flags   |= GPS_LOCATION_HAS_BEARING;
    r->fix.bearing  = str2float(tok.p, tok.end);
    return 0;
}

static int nmea_reader_update_speed( NmeaReader*  r,
                          Token        speed )
{
    Token   tok = speed;

    if (tok.p >= tok.end)
        return -1;

    r->fix.flags   |= GPS_LOCATION_HAS_SPEED;
    r->fix.speed    = 0.514444 * str2float(tok.p, tok.end);
    return 0;
}

//#define NMEA_VERBOSE
static void nmea_reader_parse( NmeaReader*  r )
{
    NmeaTokenizer  tzer[1];
    Token tok;

#ifdef NMEA_VERBOSE
    printf("%.*s\n", r->pos-1, r->in);
#endif
    if (r->pos < 9)
    {
        printf("Too short. discarded.");
        return;
    }

    nmea_tokenizer_init(tzer, r->in, r->in + r->pos);

    tok = nmea_tokenizer_get(tzer, 0);
    if (tok.p + 5 > tok.end)
    {
        printf("sentence id '%.*s' too short, ignored.", tok.end-tok.p, tok.p);
        return;
    }

    // ignore first two characters.
    //int is_bd = (tok.p[0] == 'B' && tok.p[1] == 'D');
    tok.p += 2;
    if (0)
    {
    }
    else if ( !memcmp(tok.p, "RMC", 3) )
    {
        Token  tok_time          = nmea_tokenizer_get(tzer,1);
        Token  tok_fixStatus     = nmea_tokenizer_get(tzer,2);
        Token  tok_latitude      = nmea_tokenizer_get(tzer,3);
        Token  tok_latitudeHemi  = nmea_tokenizer_get(tzer,4);
        Token  tok_longitude     = nmea_tokenizer_get(tzer,5);
        Token  tok_longitudeHemi = nmea_tokenizer_get(tzer,6);
        Token  tok_speed         = nmea_tokenizer_get(tzer,7);
        Token  tok_bearing       = nmea_tokenizer_get(tzer,8);
        Token  tok_date          = nmea_tokenizer_get(tzer,9);

        //D(LOG_DEBUG,"in RMC, fixStatus=%c", tok_fixStatus.p[0]);
        if (tok_fixStatus.p[0] == 'A')
        {

            nmea_reader_update_latlong( r, tok_latitude,
                                           tok_latitudeHemi.p[0],
                                           tok_longitude,
                                           tok_longitudeHemi.p[0] );
            //nmea_reader_update_date( r, tok_date, tok_time );

            nmea_reader_update_bearing( r, tok_bearing );
            nmea_reader_update_speed  ( r, tok_speed );
        }
    }
    else
    {
        tok.p -= 2;
        //printf("unknown sentence '%.*s", tok.end-tok.p, tok.p);
    }
#if 1
    if (r->fix.flags & GPS_LOCATION_HAS_LAT_LONG)
    {
        if (r->fix.flags & GPS_LOCATION_HAS_LAT_LONG)
        {
            printf("lat=%g lon=%g", r->fix.latitude, r->fix.longitude);
        }
        if (r->fix.flags & GPS_LOCATION_HAS_ALTITUDE)
        {
            printf(" altitude=%g", r->fix.altitude);
        }
        if (r->fix.flags & GPS_LOCATION_HAS_SPEED)
        {
            printf(" speed=%g", r->fix.speed);
        }
        if (r->fix.flags & GPS_LOCATION_HAS_BEARING)
        {
            printf(" bearing=%g", r->fix.bearing);
        }
        if (r->fix.flags & GPS_LOCATION_HAS_ACCURACY)
        {
            printf(" accuracy=%g", r->fix.accuracy);
        }
        printf("\n");
        r->fix.flags = 0;
    }
#endif
}

static void nmea_reader_addc( NmeaReader*  r, int  c ) {
    if (r->overflow) {
        r->overflow = (c != '\n');
        return;
    }

    if (r->pos >= (int) sizeof(r->in)-1 ) {
        r->overflow = 1;
        r->pos      = 0;
        return;
    }

    if ((r->pos == 0) && (c != '$')) {
        printf("ignore 0x%x - %c", c, (char)c);
        return;
    }

    r->in[r->pos] = (char)c;
    r->pos       += 1;

    if (c == '\n') {
        nmea_reader_parse( r );
        r->pos = 0;
    }
}

static void nmea_recv_urb_complete(struct urb *urb, int status) {
    struct nmea_ctx *ctx = (struct nmea_ctx *)urb->ctx;
    uint16_t i = 0;

    if (status != 0) {
        printf("%s status=%d\n", __func__, status);
        return;
    }

    for (i = 0; i < urb->actual_length; i++)
        nmea_reader_addc(&ctx->reader, urb->transfer_buffer[i]);
    usb_submit_urb(urb);
}

static int nmea_open(struct hc_info *hc_info, uint16_t idProduct) {
    struct nmea_ctx *ctx = &nmea_ctx[0];
    struct urb *urb;

    printf("%s hc_num=%u\n", __func__,
        get_hc_num(HC_BULK_IN));

    urb = &ctx->recvUrb;
    urb ->hc_type = HC_BULK_IN;
    urb ->hc_num = get_hc_num(urb ->hc_type);
    urb ->ep_size = get_ep_size(urb ->hc_type);
    urb ->urb_state = URB_STATE_IDLE;
    urb ->transfer_buffer = ctx->recvUrbBuf;
    urb ->transfer_buffer_length = RecvUrbBufLen;
    urb ->complete = nmea_recv_urb_complete;
    urb ->ctx = ctx;
    usb_start_urb(urb);

    return usb_submit_urb(urb);
}

static const struct usb_driver nmea_dirver = {
    EC20_NMEA_INTERFACE,0xFF,
    hc_mask_bulk_in,
    nmea_open,
    NULL
};

void nmea_init(void) {
    struct nmea_ctx *ctx = &nmea_ctx[0];

    nmea_reader_init(&ctx->reader);
    usb_register_drv(&nmea_dirver);
}
#endif
