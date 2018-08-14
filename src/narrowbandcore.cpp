

#include "narrowbandcore.h"
#include "serialcmds.h"

namespace Narrowband {

bool PDPContext::operator==(const PDPContext& other) const {
    return (
        (strcmp(this->APN, other.APN) == 0) &&
        (strcmp(this->type, other.type) == 0)
    );
}

NarrowbandCore::NarrowbandCore(CommandAdapter& ca_) : ca(ca_){
    clearLastStatus();
}

void NarrowbandCore::dbg_out1(const char *p, bool nl) {
#ifdef NBIOT_DEBUG1
    Serial.print("("); 
    Serial.print(p); 
    Serial.print(")"); 
    if ( nl) {
        Serial.println();
    }
#endif
}

void NarrowbandCore::clearLastStatus() {
    lastError = String("");
    lastStatusOk = lastStatusError = false;
}

bool NarrowbandCore::ready() {
    return ca.send_cmd_waitfor_reply("AT", "OK\r\n");
}

void NarrowbandCore::reboot() {
    long t = ca.getTmeout();
    ca.setTimeout(20*1000);
    ca.send_cmd_waitfor_reply("AT+NRB", "OK\r\n");
    ca.setTimeout(t);
}

void NarrowbandCore::setEcho(bool b_echo) {
    char buf[16];
    sprintf(buf,"ATE=%d", b_echo);
    ca.send_cmd_waitfor_reply(buf, "OK\r\n");
}

/** Given a module response in `buf`, size `n`, this function splits by \r\n and assigns
 * the parts to `arr_res` of `n_max_arr` elements 
 * automatically checks for "OK" and "ERROR" line and stores them in instance vars, not
 * in resulting array.
 * */
int NarrowbandCore::_split_response_array(char *buf, size_t n, char *arr_res[], int n_max_arr) {
    clearLastStatus();
    char *p1 = buf;
    char *p2 = buf;
    int i = 0;
    int j = 0;
    while( (size_t)i < n && j < n_max_arr) {
        if (*p2 == '\r' || *p2 == '\n') {
            *p2 = 0;

            if ( p2 != p1) {
                dbg_out1(p1);

                if (strcmp(p1, "OK") == 0) {
                    lastStatusOk = true;
                } else {
                    if (strstr(p1,"ERROR") != 0) {
                        lastStatusError = true;
                        lastError = String(p1);
                    } else {
                        // store result
                        arr_res[j++] = p1;
                    }
                }
            }
            p1 = p2+1;
        }

        i++;
        p2++;
    }
    if (lastStatusError) {
        dbg_out1("!!", true);
    } else {
        dbg_out1("<<",true);
    }
    return j;
}

// expects the form +CMD:v1,v2,v3,...v[n_max_arr]
// after that, *buf == command without ':', rest of pointers assigned
// destroys buf
int NarrowbandCore::_split_csv_line(char *buf, size_t n, char *arr_res[], int n_max_arr, const char *p_expect_cmdstring) {
    if ( p_expect_cmdstring != NULL) {
        char *sep = strchr(buf,':');
        int i = 0, j = 0;
        char *p = 0;
        if (buf[0] == '+' && sep != 0) {
            *sep = '\0';
            j = (sep-buf);
            p = ++sep;

            if ( strcmp(buf, p_expect_cmdstring) != 0) {
                return -2;      // unexpected command
            }

            while (sep != 0 && i < n_max_arr && (size_t)j < n) {
                arr_res[i++] = p;
                sep = strchr(p,',');
                if ( sep != 0) {
                    *sep = '\0';
                    j = (sep-buf);
                    p = ++sep;
                }
            }

            return i;
        } else {
            return -1;      // format invalid
        }
    } else {
        char *sep = buf;
        char *p = buf;
        int i = 0, j = 0;
        j = (sep-buf);

        while (sep != 0 && i < n_max_arr && (size_t)j < n) {
            arr_res[i++] = p;
            sep = strchr(p,',');
            if ( sep != 0) {
                *sep = '\0';
                j = (sep-buf);
                p = ++sep;
            }
        }

        return i;
    }

    return 0;
}


String NarrowbandCore::getModelIdentification() {
    clearLastStatus();

    const char *cmd = "AT+CGMM";
    char buf[128];
    size_t n = ca.send_cmd_recv_reply_stop(cmd, buf, sizeof(buf), "OK\r");

    char *p[4];
    int elems = _split_response_array(buf, n, p, 4);
    bool ok = (lastStatusOk && !lastStatusError && elems >= 1);
    if (ok) {
        return String(p[0]);
    }
    return String();
}

String NarrowbandCore::getManufacturerIdentification() {
    const char *cmd = "AT+CGMI";
    char buf[128];
    size_t n = ca.send_cmd_recv_reply_stop(cmd, buf, sizeof(buf), "OK\r");

    char *p[4];
    int elems = _split_response_array(buf, n, p, 4);
    bool ok = (lastStatusOk && !lastStatusError && elems >= 1);
    if (ok) {
        return String(p[0]);
    }
    return String();
}

String NarrowbandCore::getIMEI() {
    const char *cmd = "AT+CGSN";
    char buf[128];
    size_t n = ca.send_cmd_recv_reply_stop(cmd, buf, sizeof(buf), "OK\r");

    char *p[4];
    int elems = _split_response_array(buf, n, p, 4);
    bool ok = (lastStatusOk && !lastStatusError && elems >= 1);
    if (ok) {
        return String(p[0]);
    }
    return String();
}

String NarrowbandCore::getIMSI() {
    const char *cmd = "AT+CIMI";
    char buf[128];
    size_t n = ca.send_cmd_recv_reply_stop(cmd, buf, sizeof(buf), "OK\r");

    char *p[4];
    int elems = _split_response_array(buf, n, p, 4);
    bool ok = (lastStatusOk && !lastStatusError && elems >= 1);
    if (ok) {
        return String(p[0]);
    }
    return String();
}

bool NarrowbandCore::getOperatorSelection(OperatorSelectMode& mode, int& format, char *buf_operatorName) {
    mode = OperatorSelectMode::Unknown; format = -1; *buf_operatorName = '\0';

    const char *cmd = "AT+COPS?";
    char buf[256];
    size_t n = ca.send_cmd_recv_reply(cmd, buf, sizeof(buf));


    // split command lines..
    char *p[4];
    int elems = _split_response_array(buf, n, p, 4);
    bool ok = (lastStatusOk && !lastStatusError && elems >= 1);
    for ( int i = 0; ok && i < elems; i++) {
        char *q[3];
        // split line into comma-sep elements
        int n = _split_csv_line(p[i], strlen(p[i]), q, 3, "+COPS");

        if ( n >= 1) {
            int _mode = atoi(q[0]);

            switch(_mode) {
                case 0: 
                    mode = OperatorSelectMode::Automatic;
                    break;
                case 1: 
                    mode = OperatorSelectMode::Manual;
                    break;
                case 2: 
                    mode = OperatorSelectMode::Deregister;
                    break;
            }
        }
        if ( n == 3) {
            format = atoi(q[1]);
            strcpy(buf_operatorName, q[2]);
        }
        return (n>=1);
    }
    return false;
}

bool NarrowbandCore::setOperatorSelection(OperatorSelectMode mode, const char * operatorName) {
    char cmd[64];
    if ( mode == OperatorSelectMode::Manual) {
        sprintf(cmd, "AT+COPS=%d,2,%s", mode, operatorName);
    } else {
        sprintf(cmd, "AT+COPS=%d", mode);
    }
    char buf[256];
    size_t n = ca.send_cmd_recv_reply(cmd, buf, sizeof(buf));

    char *p[4];
    int elems = _split_response_array(buf, n, p, 4);
    bool ok = (lastStatusOk && !lastStatusError && elems >= 1);
    return ok;
}

bool NarrowbandCore::getModuleFunctionality(bool& fullFunctionality) {
    const char *cmd = "AT+CFUN?";
    char buf[256];

    long t = ca.getTmeout();
    ca.setTimeout(5000);

    size_t n = ca.send_cmd_recv_reply_stop(cmd, buf, sizeof(buf), "OK\r");

    ca.setTimeout(t);

    char *p[2];
    int elems = _split_response_array(buf, n, p, 2);
    bool ok = (lastStatusOk && !lastStatusError && elems >= 1);
    if (ok) {
        String res = String(p[0]);
        if (res.startsWith("+CFUN:")) {
            res = res.substring(6);
            fullFunctionality = (bool)res.toInt();
            return true;
        }
    }
    return false;
}

bool NarrowbandCore::setModuleFunctionality(const bool fullFunctionality) {
    char cmd[64];
    sprintf(cmd, "AT+CFUN=%d", fullFunctionality?1:0);
    char buf[256];

    long t = ca.getTmeout();
    ca.setTimeout(5000);

    size_t n = ca.send_cmd_recv_reply_stop(cmd, buf, sizeof(buf), "OK\r");

    ca.setTimeout(t);

    char *p[4];
    int elems = _split_response_array(buf, n, p, 4);
    bool ok = (lastStatusOk && !lastStatusError && elems >= 1);
    return ok;
}

bool NarrowbandCore::getNetworkRegistration(int& mode, int& status) {
    mode = -1; status = -1;

    const char *cmd = "AT+CEREG?";
    char buf[256];
    size_t n = ca.send_cmd_recv_reply_stop(cmd, buf, sizeof(buf), "OK\r");

    char *p[4];
    int elems = _split_response_array(buf, n, p, 4);
    bool ok = (lastStatusOk && !lastStatusError && elems >= 1);
    if (ok) {
        for ( int i = 0; i < elems; i++) {
            String line = String(p[i]), part;
            int x;
            if (line.startsWith("+CEREG:")) {
                x = line.indexOf(':');
                line = line.substring(x+1,line.length());
                x = line.indexOf(',');
                if ( x > 0) {
                    part = line.substring(0,x);
                    mode = part.toInt();
                    line = line.substring(x+1,line.length());

                    x = line.indexOf(',');
                    if ( x > 0) {
                        part = line.substring(0,x);
                        status = part.toInt();
                        //line = line.substring(x+1,line.length());

                    } else {
                        status = line.toInt();
                    }

                } else {
                    mode = line.toInt();
                }
               return true;
            }
        }
    }
    return false;
}

bool NarrowbandCore::setNetworkRegistration(const int status) {
    char cmd[64];
    sprintf(cmd, "AT+CEREG=%d", status);
    char buf[256];

    long t = ca.getTmeout();
    ca.setTimeout(5000);

    size_t n = ca.send_cmd_recv_reply_stop(cmd, buf, sizeof(buf), "OK\r");

    ca.setTimeout(t);

    char *p[4];
    _split_response_array(buf, n, p, 4);
    bool ok = (lastStatusOk && !lastStatusError);
    return ok;
}

bool NarrowbandCore::getConnectionStatus(int& urcEnabled, bool& connected) {
    urcEnabled = -1; connected = false;

    const char *cmd = "AT+CSCON?";
    char buf[256];
    size_t n = ca.send_cmd_recv_reply_stop(cmd, buf, sizeof(buf), "OK\r");

    char *p[4];
    int elems = _split_response_array(buf, n, p, 4);
    bool ok = (lastStatusOk && !lastStatusError && elems >= 1);
    if (ok) {
        for ( int i = 0; i < elems; i++) {
            String line = String(p[i]), part;
            int x;
            if (line.startsWith("+CSCON:")) {
                line = line.substring(7);
                x = line.indexOf(',');
                if ( x > 0) {
                    part = line.substring(0,x);
                    urcEnabled = part.toInt();
                    line = line.substring(x+1,line.length());
                    connected = (bool)line.toInt();
                } else {
                    urcEnabled = line.toInt();
                }   
                return true;
            }
        }
    }
    return false;
}

bool NarrowbandCore::setConnectionStatus(const bool connected) {
    char cmd[64];
    sprintf(cmd, "AT+CSCON=%d", connected?1:0);
    char buf[256];

    long t = ca.getTmeout();
    ca.setTimeout(5000);

    size_t n = ca.send_cmd_recv_reply_stop(cmd, buf, sizeof(buf), "OK\r");

    ca.setTimeout(t);

    char *p[4];
    _split_response_array(buf, n, p, 4);
    bool ok = (lastStatusOk && !lastStatusError);
    return ok;
}

bool NarrowbandCore::getAttachStatus(bool& attached) {
    attached = false;

    const char *cmd = "AT+CGATT?";
    char buf[256];
    size_t n = ca.send_cmd_recv_reply_stop(cmd, buf, sizeof(buf), "OK\r");

    char *p[4];
    int elems = _split_response_array(buf, n, p, 4);
    bool ok = (lastStatusOk && !lastStatusError && elems >= 1);
    if (ok) {
        for ( int i = 0; i < elems; i++) {
            String line = String(p[i]), part;
            if (line.startsWith("+CGATT:")) {
                line = line.substring(7);
                attached = (bool)line.toInt();
                return true;
            }
        }
    }
    return false;
}

bool NarrowbandCore::setAttachStatus(const bool attached) {
    char cmd[64];
    sprintf(cmd, "AT+CGATT=%d", attached?1:0);
    char buf[256];

    long t = ca.getTmeout();
    ca.setTimeout(5000);

    size_t n = ca.send_cmd_recv_reply_stop(cmd, buf, sizeof(buf), "OK\r");

    ca.setTimeout(t);

    char *p[4];
    _split_response_array(buf, n, p, 4);
    bool ok = (lastStatusOk && !lastStatusError);
    return ok;
}

bool NarrowbandCore::getSignalQuality(int& rssi, int& ber) {
    rssi = -1; ber = -1;

    const char *cmd = "AT+CSQ";
    char buf[256];
    size_t n = ca.send_cmd_recv_reply_stop(cmd, buf, sizeof(buf), "OK\r");

    // split command lines..
    char *p[4];
    int elems = _split_response_array(buf, n, p, 4);
    bool ok = (lastStatusOk && !lastStatusError && elems >= 1);
    for ( int i = 0; ok && i < elems; i++) {
        char *q[2];
        // split line into comma-sep elements
        int n = _split_csv_line(p[i], strlen(p[i]), q, 2, &cmd[2]);

        if ( n == 2) {
            rssi = atoi(q[0]);
            ber = atoi(q[1]);
            return true;
        }
    }
    return false;
}

int NarrowbandCore::getPDPContexts(PDPContext *arrContext, size_t sz_max_context) {
    const char *cmd = "AT+CGDCONT?";
    char buf[1024];
    size_t n = ca.send_cmd_recv_reply_stop(cmd, buf, sizeof(buf), "OK\r");
    int j = 0;

    // split command lines..
    char *p[12];
    int elems = _split_response_array(buf, n, p, 12);
    bool ok = (lastStatusOk && !lastStatusError && elems >= 1);
    for ( int i = 0; ok && i < elems; i++) {
        char *q[4];
        // split line into comma-sep elements
        int n = _split_csv_line(p[i], strlen(p[i]), q, 4, "+CGDCONT");

        if ( n >= 3) {
            PDPContext& p = arrContext[j++];
            memset(&p,0,sizeof(p) );
            p.cid = atoi(q[0]);

            char *x = q[1];
            int l = strlen(x);
            if ( l > 0) {
                if (x[l-1] == '"') {
                    x[l-1] = 0;
                    l--; 
                }
                if (*x == '"') {
                    x++; l--;
                }
                strncpy(p.type, x, l); 
            }

            x = q[2];
            l = strlen(x);
            if ( l > 0) {
                if (x[l-1] == '"') {
                    x[l-1] = 0;
                    l--; 
                }
                if (*x == '"') {
                    x++; l--;
                }
                strncpy(p.APN, x,l);
            }
        }
    }
    return j;

}

bool NarrowbandCore::addPDPContexts(PDPContext& ctx) {
    char cmdbuf[128];
    sprintf(cmdbuf, "AT+CGDCONT=%d,\"%s\",\"%s\"", ctx.cid, ctx.type, ctx.APN);

    char buf[128];
    ca.send_cmd_recv_reply_stop(cmdbuf, buf, sizeof(buf), "OK\r");
    return (lastStatusOk && !lastStatusError);
}

bool NarrowbandCore::setCDPServer(const String host, const int port) {
    char cmd[64];
    sprintf(cmd, "AT+NCDP=%s,%d", host.c_str(), port);
    char buf[256];
    size_t n = ca.send_cmd_recv_reply(cmd, buf, sizeof(buf));

    char *p[4];
    _split_response_array(buf, n, p, 4);
    bool ok = (lastStatusOk && !lastStatusError);
    return ok;

}

int NarrowbandCore::createSocket( SocketType s, int protocol, int listenPort, bool bWithReceiveControl) {
    char tbuf[16];
    memset(tbuf,0,sizeof(tbuf));
    if ( s == SocketType::Datagram) {
        strcpy(tbuf, "DGRAM");
    }
    if ( s == SocketType::Raw) {
        strcpy(tbuf, "RAW");
    }

    char rc[8];
    memset(rc,0,sizeof(rc));
    if(bWithReceiveControl) {
        strcpy(rc,",1");
    }

    char cmdbuf[128];
    sprintf(cmdbuf, "AT+NSOCR=%s,%d,%d%s", tbuf,protocol,listenPort,rc);
    char buf[256];
    size_t n = ca.send_cmd_recv_reply(cmdbuf, buf, sizeof(buf));

    char *p[4];
    int elems = _split_response_array(buf, n, p, 4);
    bool ok = (lastStatusOk && !lastStatusError && elems >= 1);
    if ( ok) {
        for ( int i = 0; i < elems; i++) {
            char *line = p[i];
            if (line && *line >= '0' && *line <= '9') {
                return atoi(line);
            }
        }
        return -2;          // no socket id recognized
    } else {
        return -1;          // did not return with OK
    }
}

bool NarrowbandCore::closeSocket( int socket) {
    char cmdbuf[128];
    sprintf(cmdbuf, "AT+NSOCL=%d", socket);

    char buf[128];
    ca.send_cmd_recv_reply_stop(cmdbuf, buf, sizeof(buf), "OK\r");
    return (lastStatusOk && !lastStatusError);

}

bool NarrowbandCore::sendTo(int socket, const char *remoteAddr, int remotePort, size_t length, const uint8_t *p_data) {
    size_t n = 1+length*2;
    char *hexbuf = (char*)malloc(n); 
    memset(hexbuf,0,n);

    char *q = hexbuf;
    for ( size_t i = 0; i < length; i++) {
        sprintf(q, "%.2X", p_data[i]);
        q += 2;
    }

    char cmdbuf[128];
    sprintf(cmdbuf, "AT+NSOST=%d,%s,%d,%d,%s", socket, remoteAddr, remotePort, length, hexbuf);
    char buf[256];
    size_t n2 = ca.send_cmd_recv_reply_stop(cmdbuf, buf, sizeof(buf), "OK\r");

    free(hexbuf);

    char *p[4];
    int elems = _split_response_array(buf, n2, p, 4);
    return (lastStatusOk && !lastStatusError && elems >= 1);
/*    return ok;
    if ( ok) {
        for ( int i = 0; i < elems; i++) {
            char *line = p[i];
            if (line && *line >= '0' && *line <= '9') {
                return atoi(line);
            }
        }
        return -2;          // no socket id recognized
    } else {
        return -1;          // did not return with OK
    }
*/
}

uint8_t charToNum(char c) {
  if ( c >= '0' && c <= '9') {
   return (uint8_t)(c-'0'); 
  }
  if ( c >= 'a' && c <= 'f') {
   return 10+(uint8_t)(c-'a'); 
  }
  if ( c >= 'A' && c <= 'F') {
   return 10+(uint8_t)(c-'A'); 
  }
  return 0;
}

void hexstringToByteArr(const char *p, int n, uint8_t *res, int len) {
  uint8_t *r = res;
  int i = 0;
  while ( p && *p && i < n && i < len*2) {
    uint8_t v = charToNum(p[i])*16 + charToNum(p[i+1]);
    
    *(r++) = v;
    i += 2;
  }
}


bool NarrowbandCore::recv(int socket, uint8_t *buf, size_t sz_buf, unsigned long timeout) {
    char cmdbuf[128];
    sprintf(cmdbuf, "AT+NSORF=%d,%d", socket, sz_buf);

    char rspbuf[2048];
    unsigned long l = ca.getTmeout();
    ca.setTimeout(timeout);
    size_t n2 = ca.send_cmd_recv_reply_stop(cmdbuf, rspbuf, sizeof(rspbuf), "OK\r\n");
    ca.setTimeout(l);

    char *p[4];
    int elems = _split_response_array(rspbuf, n2, p, 4);
    for ( int i = 0; i < elems; i++) {
        char *q[6];
        int j = _split_csv_line(p[i], strlen(p[i]),q, 6);
        if (j > 0) {
            int rsocket = atoi(q[0]);
            if ( rsocket == socket) {
                // q[1] remote IP
                // q[2] remote Port

                //int bytes = atoi(q[3]);

                // hex-parse q[4]
                hexstringToByteArr(q[4], strlen(q[4]), buf, sz_buf);
                return true;
            }
        }
    }

    return false;
}

size_t NarrowbandCore::waitForMessageIndication(int socket, unsigned long timeout) {
    char buf[128];
    unsigned long l = ca.getTmeout();
    ca.setTimeout(timeout);
    size_t n = ca.send_cmd_recv_reply("", buf, sizeof(buf));
    ca.setTimeout(l);
    char *p[4];
    int elems = _split_response_array(buf, n, p, 4);
    // no ok, direkt nsonmi msg.
    for ( int i = 0; i < elems; i++) {
        char *q[3];
        int j = _split_csv_line(p[i], strlen(p[i]),q, 3, "+NSONMI");
        if (j > 0) {
            int rsocket = atoi(q[0]);
            int bytes = atoi(q[1]);
            if ( rsocket == socket) {
                return bytes;
            }
        }
    }
    return 0;
}

int NarrowbandCore::waitForResponse(unsigned long timeout, void(*cb_modem_msg)(const char *p_msg_line, const void *ctx)) {
    char buf[256];
    unsigned long l = ca.getTmeout();
    ca.setTimeout(timeout);
    size_t n = ca.send_cmd_recv_reply("", buf, sizeof(buf));
    ca.setTimeout(l);
    char *p[4];
    int elems = _split_response_array(buf, n, p, 4);
    if ( cb_modem_msg != NULL) {
        for ( int i = 0; i < elems; i++) {
            cb_modem_msg(p[i], this);
        }
    }
    return elems;
}

bool NarrowbandCore::ping(const char *ip, const long timeout_msec) {
    return false;

}

}

