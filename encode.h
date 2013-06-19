/* 
 * File:   encode.h
 * Author: lorddoskias
 *
 * Created on June 18, 2013, 9:27 PM
 */

#ifndef ENCODE_H
#define	ENCODE_H

#ifdef	__cplusplus
extern "C" {
#endif

void *write_thread(void *ctx);
void *decode_thread(void *ctx);

#ifdef	__cplusplus
}
#endif

#endif	/* ENCODE_H */

