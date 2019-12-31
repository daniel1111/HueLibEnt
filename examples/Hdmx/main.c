/*
 * 
 * Copyright (c) 2019, Ian Stephenson <ian@dctsystems.co.uk>
 *
 * Based on code by:
 * Copyright (c) 2019, Daniel Swann <github@dswann.co.uk>
 * All rights reserved.
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 * 
 * 1. Redistributions of source code must retain the above copyright notice, this
 *    list of conditions and the following disclaimer.
 * 
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <stdio.h>
#include <unistd.h>
#include <ifaddrs.h>


#include "dtls.h"
#include "hue_entertainment.h"
#include "hue_rest.h"

#define DTLS_PORT 2100
#define SSL_PORT  443

#define ARTNET_PORT 6454
#define MAX_ARTNET_SIZE 1024
#define ARTNET_OP_POLL 0x20
#define ARTNET_OP_POLL_REPLY 0x21
#define ARTNET_OP_DMX 0x50
#define ARTNET_OP_TOD_REQUEST 0x80

void print_usage(const char* name)
{
  printf("\nArt-Net to Hue lights Bridge\n");
  printf("Usage: %s -a <ip address> -e <area> -i <identity> -p <psk> [-d <debug level>]\n\n", name);

  printf("Parameters:\n");
  printf("    -a <ip address>  IP address of Hue Bridge\n");
  printf("    -e <area>        Entertainment Area Number\n");
  printf("    -i <identity>    Identity to use when connecting to bridge\n");
  printf("    -p <psk>         Pre Shared Key to use when connecting to bridge\n");
  printf("    -d <level>       Debug level 0-3. Default: 1 (errors only)\n");
  printf("\n");
}

void sendPollReply(struct sockaddr_in *sender,int artnetSock)
{


    struct sockaddr_in     servaddr; 
    memset(&servaddr, 0, sizeof(servaddr)); 
      
    // Filling server information 
    servaddr.sin_family = AF_INET; 
    servaddr.sin_port = sender->sin_port; 
    servaddr.sin_addr = sender->sin_addr; 
      
    uint8_t buffer[240]; 
    memset(buffer,0,240);
    sprintf(buffer,"Art-Net");
    buffer[8]=0;
    buffer[9]=ARTNET_OP_POLL_REPLY;
    //My IP
    struct ifaddrs * ifAddrStruct = NULL;
    getifaddrs(&ifAddrStruct);
    struct ifaddrs * ifa = NULL;
    for (ifa = ifAddrStruct; ifa != NULL; ifa = ifa->ifa_next)
	{
	if (ifa->ifa_addr->sa_family == AF_INET)
		{
		struct in_addr addr;
		addr=((struct sockaddr_in *)ifa->ifa_addr)->sin_addr;
		if(*(unsigned char *)&addr!=127)
			{
			*(uint32_t *)(buffer+10)=addr.s_addr;
			//printf("%d.%d.%d.%d\n",buffer[10],buffer[11],buffer[12],buffer[13]);
			}
		}
	}
    if (ifAddrStruct != NULL)
	freeifaddrs(ifAddrStruct);
    //My Port
    buffer[15]=ARTNET_PORT& 0xff;;
    buffer[14]=ARTNET_PORT>>8;;
    //Pretty names
    sprintf(buffer+26,"Philips Hue");
    sprintf(buffer+44,"Art-Net to Philips Hue Bridge");
    //Number of Ports
    buffer[173]=1;
    buffer[174]=0x80;
    buffer[182]=0x80;
    buffer[190]=0x01;

    //BindIP?
    buffer[207]=buffer[10];
    buffer[208]=buffer[11];
    buffer[209]=buffer[12];
    buffer[210]=buffer[13];



    int n=sendto(artnetSock, buffer, 240, 
        MSG_CONFIRM, (const struct sockaddr *) &servaddr,  
            sizeof(servaddr)); 
    if(n==-1)
	{
	    printf("Poll Reply Error %d\n",n);
	    exit(1);
	}

}

int main (int argc, char **argv)
{
  const char *identity = NULL;
  const char *psk = NULL;
  const char *ip_address = NULL;
  int area=0;
  int c;
  int debug_level = MSG_ERR;
  struct dtls_ctx ctx_dtls;
  struct hue_ent_ctx ctx_ent;
  struct hue_rest_ctx ctx_hr;
  struct hue_entertainment_area *ent_areas;
  void *msg_buf;
  int buf_len;
  int ent_areas_count;
  int framerate = 80;
  int interval_ms;

  while ((c = getopt (argc, argv, "i:p:a:d:e:")) != -1)
  {
    switch (c)
      {
      case 'i': /* Identity */
        identity = optarg;
        break;

      case 'p': /* Pre-shared key */
        psk = optarg;
        break;

      case 'a': /* Address */
        ip_address = optarg;
        break;

      case 'd': /* Debug level */
        debug_level = atoi(optarg);
        break;

      case 'e': /* Entertainment Area */
        area = atoi(optarg);
        break;

      case 'h':
      case 'H':
        print_usage(argv[0]);
        exit(0);
        break;

      case '?':
        print_usage(argv[0]);
        exit(-1);
        break;

      default:
        return -1;
      }
  }

  if (!identity)
  {
    printf("\nERROR: Identity not provided\n");
    print_usage(argv[0]);
    return -1;
  }

  if (!psk)
  {
    printf("\nERROR: psk not provided\n");
    print_usage(argv[0]);
    return -1;
  }

  if (!ip_address)
  {
    printf("\nERROR: IP address not provided\n");
    print_usage(argv[0]);
    return -1;
  }

  hue_rest_init();
  hue_rest_init_ctx(&ctx_hr, NULL, ip_address, SSL_PORT, identity, debug_level);

  printf("Getting entertainment areas\n");
  hue_rest_get_ent_groups(&ctx_hr, &ent_areas, &ent_areas_count);

  if (ent_areas_count <= 0)
  {
    printf("No entertainment areas found.\n");
    hue_rest_cleanup_ctx(&ctx_hr);
    hue_rest_cleanup();
    return -1;
  }

  if(area<0 || area>=ent_areas_count)
  {
    printf("Invalid Area Number.\n");
    hue_rest_cleanup_ctx(&ctx_hr);
    hue_rest_cleanup();
    return -1;
  }

  /* Count how many lights are in the entertainment area */
  int light_count;
  for (light_count=0; light_count < MAX_LIGHTS_PER_AREA; light_count++)
    if (ent_areas[area].light_ids[light_count] == 0)
      break;

  if (light_count == 0)
  {
    printf("No lights found in entertainment area [%s].\n", ent_areas[area].area_name);
    hue_rest_cleanup_ctx(&ctx_hr);
    hue_rest_cleanup();
    return -2;
  }
  printf("%d lights found in entertainment area [%s].\n", light_count,ent_areas[area].area_name);

  /* Activate the entertainment area */
  printf("Enabling entertainment area [%s]\n", ent_areas[area].area_name);
  hue_rest_activate_stream(&ctx_hr, ent_areas[area].area_id);

  /* Initialise hue entertainment context */
  hue_ent_init(&ctx_ent, light_count);

  /* Assign the light ID (as returned by the bridge) to each light to be controlled (0..n) */
  for (int n = 0; n < light_count; n++)
    hue_ent_set_light_id(&ctx_ent, n, ent_areas[area].light_ids[n]);

  /* Connect to bridge using DTLS */
  printf("Making DTLS connection to bridge\n");
  dtls_init(&ctx_dtls, identity, psk, NULL, debug_level);
  int retval = dtls_connect(&ctx_dtls, ip_address, DTLS_PORT);
  if (retval)
  {
    printf("Failed to make DTLS connection to bridge (retval=%d)\n", retval);
    hue_rest_cleanup_ctx(&ctx_hr);
    hue_rest_cleanup();
    dtls_cleanup(&ctx_dtls);
    hue_ent_cleanup(&ctx_ent);
    return -3;
  }


   /* Hue Is Set Up */

   /* Now listen on UDP */
   int artnetSock; 
    char buffer[1024]; 
    struct sockaddr_in servaddr, cliaddr; 
      
    // Creating socket file descriptor 
    if ( (artnetSock = socket(AF_INET, SOCK_DGRAM, 0)) < 0 ) { 
        perror("socket creation failed"); 
        exit(EXIT_FAILURE); 
    } 
      
    memset(&servaddr, 0, sizeof(servaddr)); 
    memset(&cliaddr, 0, sizeof(cliaddr)); 
      
    // Filling server information 
    servaddr.sin_family    = AF_INET; // IPv4 
    servaddr.sin_addr.s_addr = INADDR_ANY; 
    servaddr.sin_port = htons(ARTNET_PORT); 
      
    // Bind the socket with the server address 
    if ( bind(artnetSock, (const struct sockaddr *)&servaddr,  
            sizeof(servaddr)) < 0 ) 
    { 
        perror("bind failed"); 
        exit(EXIT_FAILURE); 
    } 

    struct timeval tv;
    tv.tv_sec = 5;
    tv.tv_usec = 0;
    setsockopt(artnetSock, SOL_SOCKET, SO_RCVTIMEO,&tv,sizeof(tv));


   unsigned char sequence;
    while(1)
	    {
	    int upd_len = sizeof(cliaddr);  //len is value/resuslt 
	  
	    int n = recvfrom(artnetSock, (char *)buffer, MAX_ARTNET_SIZE,  
			0, ( struct sockaddr *) &cliaddr, 
			&upd_len); 
	    if(n>10)
		{
		    //Magic Number
		    if(buffer[7]!=0 || strcmp(buffer,"Art-Net")!=0)
			{
			printf("Bad Magic\n");
			continue;
			}

		    //Opcode
		    int opCodeHandled;
		    if(buffer[8]!=0)
			{
			printf("Bad Opcode\n");
			continue;
			}

		    switch(buffer[9])
			{
			case ARTNET_OP_POLL:
				{
				if(debug_level>=MSG_INFO)
					{
					printf("Poll:");
					printf("%s:",inet_ntoa(cliaddr.sin_addr));
					printf("%d\n",ntohs(cliaddr.sin_port));
					}
				sendPollReply(&cliaddr,artnetSock);
				break;
				}
			case ARTNET_OP_TOD_REQUEST:
				{
				if(debug_level>=MSG_INFO)
					printf("TodRequest\n");

				break;;
				}
			case ARTNET_OP_POLL_REPLY:
				{
				if(debug_level>=MSG_INFO)
					{
					printf("Poll Reply\n");
					printf("%s\n",buffer+26);
					printf("%s\n",buffer+44);
					printf("%s\n",buffer+108);
					break;
					}
				}

			case ARTNET_OP_DMX:
				{
				//Protocaol (14)
				 if(buffer[10]!=0)
					break;
				 if(buffer[11]!=14)
					break;


				int dmx_count=buffer[16];
				dmx_count=(dmx_count<<8)+buffer[17];
				if(debug_level>=MSG_DEBUG)
					printf("Got %d Channel Data\n",dmx_count);

				if(dmx_count>3*light_count)
					dmx_count=3*light_count;

				unsigned char *data=buffer+18;
				    int light;

				    for(light=0;light<dmx_count/3;light++)
				    {
				    int r,b,g;
				    r=data[0];
				    g=data[1];
				    b=data[2];

				    hue_ent_set_light(&ctx_ent, light, r << 8, g << 8, b << 8);
				    data+=3;
				    }
				}
			    break;
		    default:
			    printf("Unknown Packet:%x:%x\n",buffer[8],buffer[9]);
			}
		}
		
	    /* Generate message */
	    hue_ent_get_message(&ctx_ent, &msg_buf, &buf_len);

	    /* Send message */
	    if (dtls_send_data(&ctx_dtls, msg_buf, buf_len))
	    {
	      printf("Connection lost, exiting...\n");
	      break;
	    }
	  }

  hue_rest_cleanup_ctx(&ctx_hr);
  hue_rest_cleanup();
  dtls_cleanup(&ctx_dtls);
  hue_ent_cleanup(&ctx_ent);

  return 0;
}
