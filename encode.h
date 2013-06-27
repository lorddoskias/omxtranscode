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
    
void *consumer_thread(void *thread_ctx);
void *decode_thread(void *ctx);
void *writer_thread(void *thread_ctx);

#ifdef	__cplusplus
}
#endif

#endif	/* ENCODE_H */

