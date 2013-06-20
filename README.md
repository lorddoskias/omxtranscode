omxtranscode
============

OpenMAX IL based transcoder 

This is a simple OpenMAX IL based transcoder for the raspberry pi. Currently it has the ability to demux an input
mpeg4 stream and encode it to an mpeg4 stream with 1mbit vbr with 640x480. This serves just as a proof of concept. 


It uses code from pidvbip and omxtx. Hopefully my version is a little bit more readable. The application is 
multithreaded. It has the following threads: 

**demux_thread** - opens the input file and demuxes only the video by passing it to a shared blocking queue

**decode_thread** - this thread continually dequeus demuxed video packets and feeds them to a pipeline which consists of video_decode -> resize -> video_encode. 

**consumer_thread** - this thread continually feeds buffers to the encoder which then get put into yet another blocking queue which holds processed data

**writer_thread** - this thread deques processed packets and writes them to the output file

Currently I make no effort to cleanup the OpenMax components. Hopefully this will help people become more familiar
with the process of setting up openmax pipelines. 


**MakeFile** - Currently I do not supply the make file since my configuration is: develop inside netbeans with
the toolchain directly compiling on the PI. I have also moved the libraries and include file from the /opt/vc 
directory into my local tree so that everything is seamless. Apart from this, this code should compile with the 
default switches which are present in Makefile.include inside the /opt/vc/src/hello_pi dir.


Copyright
==========
(C) Nikolay Borisov 2013

This program is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation; either version 2 of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License along with this program; if not, write to the Free Software Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
