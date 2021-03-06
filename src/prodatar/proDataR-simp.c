/*attribute on dataset
 * 
 * cross analysis simulation code.
 * the cache part, in the future, will be connected with a database
 * Jialin Liu 
 *
 *
 *system runtime routine
 *Step one  : Read cache based on percentage, determine the optimized I/O
 *Step two  : Open raw datasets
 *Step three: Perform the calculation
 *Step four : Perform the high-level chunking
 *Step five : Store the sub-results
 *Step six  : Construct the final result
 *
 *Date: May 2013
 *Version 2: Apr 2014
 */

#include <stdlib.h>
#include <string.h>
#include <mpi.h>
#include <pnetcdf.h>
#include <stdio.h>
#include <float.h>


#include "getopt.h"
#include "timer.h"
#include "connectdb.h"
#include "readdb.h"
#include "writedb.h"
#include <libmemcached/memcached.h>

#define TRUE 1
#define FALSE 0
#define LAT_NAME "latitude"
#define LON_NAME "longitude"
#define REC_NAME "time"
#define LVL_NAME "level"

#define NAME_MAX 255
#define CACHE_SIZE 14000
#define TEMP_NAME "temperature"
#define UNITS "units"
#define DEGREES_EAST "degrees_east"
#define DEGREES_NORTH "degrees_north"
#define CACHE_FILE "cachefile1"

#define NDIMS 4
char FILENAME[NAME_MAX];
char SERVER_STRING[NAME_MAX];
long long total_entry;
long long total_search;
int total_db;
int io=0;
int db=0;
int new_analysis_range[8];
int s_time,s_level,s_lat,s_lon;
int l_time,l_level,l_lat,l_lon;
static void handle_error(int status)
{
	fprintf(stderr, "%s\n", ncmpi_strerror(status));
	exit(-1);
}
void parseInput(int argc, char **argv);


int main(int argc, char **argv) {

    int rank, nprocs;
    int ret, ndims, nvars, ngatts, unlimited;
    int NLVL,NREC,NLAT,NLON;
    size_t start[NDIMS], count[NDIMS];
    MPI_Offset *mpi_start, *mpi_count;
    int *int_start, *int_count;
    MPI_Offset *dim_sizes, var_size;
    int ncid, pres_varid, temp_varid;
    int lvl,lat,lon,rec, i,ilvl,ilat,ilon,inec;
    struct timeval start_time[20];
    float elapse[20];
    float * temp_in;
    float temp_sum;
    char * op="sum";
    memcached_st *memc;
    memcached_return rc;
    char *dim=(char *)malloc(sizeof(char)*100);
    if(!dim) fprintf(stderr,"malloc error dim");
    size_t request_size;
    int total_query=0;
    int chunk_size=10;
      memcached_server_st *servers = NULL;
    //const char * server_string="localhost:11221";
 //INPUT PARSING
   parseInput(argc,argv);
//MPI_Init
    MPI_Init(&argc, &argv);
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &nprocs);
//DB CONNECTION
    MPI_Barrier(MPI_COMM_WORLD);
    timer_on(0);
    const char * server_string;
    if(rank==0&&db==1){
	switch (total_db){
	case 1:
		server_string="10.6.63.56:11211";
		break;
	case 2:
		server_string="10.6.63.56:11211,10.6.63.55:11211";
		break;
	case 3:
		server_string="10.6.63.56:11211,10.6.63.55:11211,10.6.63.36:11211";
		break;
	case 4:
		server_string="10.6.63.56:11211,10.6.63.55:11211,10.6.63.36:11211,10.6.63.58:11211";
		break;		
	default:
		server_string="10.6.63.56:11211";
		break;
	}

	memc= memcached_create(NULL);
	servers=memcached_servers_parse(server_string);
	rc= memcached_server_push(memc,servers);
	rc= memcached_flush(memc,0);
	if (rc == MEMCACHED_SUCCESS)
	  fprintf(stderr,"Added server successfully\n");
	else
	  fprintf(stderr,"Couldn't add server: %s\n",memcached_strerror(memc, rc));
    }
    MPI_Barrier(MPI_COMM_WORLD);
    timer_off(0);
//DB Write  
    MPI_Barrier(MPI_COMM_WORLD);
    timer_on(1);
    if(rank==0&&db==1){
  
    char range0[]=",0 0 0 100 200 300 ";
    char *temp_key=malloc(sizeof(char *));
    for(i=0;i<total_entry;i++){
     sprintf(temp_key,"%lld",i);
     rc=memcached_set(memc,temp_key,strlen(temp_key),range0,strlen(range0),(time_t)0,(uint32_t)0);
    }
    size_t size_entry=sizeof(int)+sizeof(range0);
    double total_size=(double)total_entry*(double)size_entry/1024.0/1024.0;//in MBs
    if(rc==MEMCACHED_SUCCESS)
	 fprintf(stderr,"Added value successfully\n");  
    else
	fprintf(stderr,"Added value failed\n",memcached_strerror(memc, rc));
    }
    MPI_Barrier(MPI_COMM_WORLD);
    timer_off(1);
//FILE OPEN
    timer_on(2);
    ret = ncmpi_open(MPI_COMM_WORLD, FILENAME, NC_NOWRITE, MPI_INFO_NULL,
                    &ncid);
    MPI_Barrier(MPI_COMM_WORLD);
    timer_off(2);
    if (ret != NC_NOERR) handle_error(ret);
    if ((ret = ncmpi_inq_varid(ncid, TEMP_NAME, &temp_varid)))
       if (ret != NC_NOERR) handle_error(ret);

    ret = ncmpi_inq(ncid, &ndims, &nvars, &ngatts, &unlimited);
    if (ret != NC_NOERR) handle_error(ret);
    
    dim_sizes = calloc(ndims, sizeof(MPI_Offset));
  
    for(i=0; i<ndims; i++)  {
            ret = ncmpi_inq_dimlen(ncid, i, &(dim_sizes[i]) );
            if (ret != NC_NOERR) handle_error(ret);
    }
//DATA READ 
    NLVL=(int)dim_sizes[0];
    NLAT=(int)dim_sizes[1];
    NLON=(int)dim_sizes[2];
    NREC=(int)dim_sizes[3];
    mpi_start = (MPI_Offset *)malloc(ndims*sizeof(MPI_Offset));
    mpi_count = (MPI_Offset *)malloc(ndims*sizeof(MPI_Offset));
    //parallelize along the slowest dimension
    mpi_count[0] = l_time/nprocs;    
    if(rank==nprocs-1){
    mpi_count[0] = l_time/nprocs+l_time%nprocs;
    }
    //set up the 3D cube for each process
    mpi_count[1] = l_level;
    mpi_count[2] = l_lat;
    mpi_count[3] = l_lon;     
    mpi_start[0] = s_time+rank*mpi_count[0];
    mpi_start[3] = s_lon;  
    mpi_start[2] = s_lat;
    mpi_start[1] = s_level;	      
    //buffer for each process
    request_size=mpi_count[0]*l_lon*l_lat*l_level*sizeof(float);
    temp_in=(float *)malloc(request_size);
    if(!temp_in) fprintf(stderr,"temp_in malloc error");
    MPI_Barrier(MPI_COMM_WORLD);
    timer_on(3);
    //independent I/O
    if(io==0)
    {
   	 ret=ncmpi_begin_indep_data(ncid);
  	 if (ret != NC_NOERR) handle_error(ret);
  	 ret = ncmpi_get_vara_float(ncid, temp_varid, mpi_start,
                                    mpi_count, temp_in);
  	 if (ret != NC_NOERR) handle_error(ret);
 	 ret=ncmpi_end_indep_data(ncid);
   	 if (ret != NC_NOERR) handle_error(ret);
    }
    //Collective I/O
    else if(io==1)
    {
	 ret = ncmpi_get_vara_float_all(ncid, temp_varid, mpi_start,
                                    mpi_count, temp_in);
         if (ret != NC_NOERR) handle_error(ret);
    }
    
    MPI_Barrier(MPI_COMM_WORLD);
    timer_off(3);
//COMPUTE
    MPI_Barrier(MPI_COMM_WORLD);
    timer_on(4);
    for(inec=0;inec<mpi_count[0];inec++){
	for(ilvl=0;ilvl<NLVL;ilvl++){
		for(ilat=0;ilat<NLAT;ilat++){
			for(ilon=0;ilon<NLON;ilon++){
		         temp_sum+=*(temp_in+inec*NLVL*NLAT*NLON+ilvl*NLAT*NLON+ilat*NLON+ilon);
			}
		}
	}
     temp_sum=0;
    }
    if(temp_in) free(temp_in);
    MPI_Barrier(MPI_COMM_WORLD);
    timer_off(4);
//META READ
    MPI_Barrier(MPI_COMM_WORLD);
    timer_on(5);
    uint32_t flags;
    char *searchkeys[100];
    size_t key_length[100];
    for(i=0;i<100;i++){
	searchkeys[i]=malloc(sizeof(char *));
    	key_length[i]=strlen(searchkeys[i]);
    }
    char return_key[MEMCACHED_MAX_KEY];
    size_t return_key_length;
    char *return_value;
    size_t return_value_length;


    if(rank==0&&db==1){    

    if (rc == MEMCACHED_SUCCESS){
    	for(i=0;i<total_search/100;i++){
	for(keyi=0;keyi<100;keyi++)
	sprintf(searchkeys[i],"%d",i*100+keyi);


  	rc= memcached_mget(memc, searchkeys, key_length, 100);

  	if (rc == MEMCACHED_SUCCESS)
    	{
    	  while ((return_value= memcached_fetch(memc, return_key, &return_key_length,
                                            &return_value_length, &flags, &rc)) != NULL)
    	    {
    	      if (rc == MEMCACHED_SUCCESS)
     	       {
           	   fprintf(stderr,"Key %s returned %s\n",return_key, return_value);
         	   }
       	 }
    	}
        }
    }
    if(rc==MEMCACHED_SUCCESS)
    printf("search success\n");
    }
    MPI_Barrier(MPI_COMM_WORLD);
    timer_off(5);
//TIME PRINT
    if(rank==0)
    {
        float request_mb = request_size/1024.0/1024.0;
	printf("\n\n************Data************\n");
    	printf("Dataset(time:level:lat:lon)(%d:%d:%d:%d)\n",NREC,NLVL,NLAT,NLON);
        printf("Request range/process:(time:level:lat:lon)(%d:%d:%d:%d)\n",
	(int)mpi_count[0],(int)mpi_count[1],(int)mpi_count[2],(int)mpi_count[3]);
	printf("Request size/process %f MB, or %f GB\n",request_mb,request_mb/1024.0);
	printf("Request size/node %f Gb, Memory Capacity 24G\n",request_mb/1024.0*12);
	printf("Number of Processes %d \n",nprocs);
	printf("Number of Nodes %d \n",nprocs/12);
	printf("Total Request Size %f MB, or %f GB\n",nprocs*request_mb,nprocs*request_mb/1024.0);
	printf("\n\n************Time************\n");
        if(db==1){
	printf("Total Number of DBs %d\n",total_db);
	printf("DataBase Connection Time: %f \n",elapse[0]);       
	printf("DataBase Parallel Query Time: %f \n",elapse[5]);    	
        printf("DataBase Query Numbers: %lld \n",total_search);
	printf("DataBase Write Time: %f \n",elapse[1]);
	printf("DataBase Write Numbers:%lld\n",total_entry);
	}
	printf("File Open Cost:%f\n",elapse[2]);
   	printf("I/O MODE(0:ind,1:coll): %d I/O, cost: %f \n",io,elapse[3]);
	printf("Bandwidth:%f MB/s %f GB/s\n",nprocs*request_mb/elapse[3],nprocs*request_mb/1024.0/elapse[3]);
	printf("Compute Cost:%f\n",elapse[4]);
	printf("\n\n************END************\n");
    }
    ret = ncmpi_close(ncid);
    if (ret != NC_NOERR) handle_error(ret);
    MPI_Finalize();
    return 0;
}

void parseInput(int argc, char **argv){
 int c;
 while ((c = getopt (argc, argv, "l:a:b:c:d:e:f:g:h:s:i:j:k:m:n:")) != -1)
    switch (c)
      {
      case 'l':
      strncpy(FILENAME,optarg,NAME_MAX);
      break;
      case 'a':
	s_time = strtol(optarg, NULL, 10);
	new_analysis_range[0]=s_time;
	break;
      case 'b':
	s_level = strtol(optarg, NULL, 10);
	new_analysis_range[1]=s_level;
	break;
      case 'c':
	s_lat = strtol(optarg, NULL, 10);
	new_analysis_range[2]=s_lat;
	break;
      case 'd':
	s_lon = strtol(optarg, NULL, 10);
	new_analysis_range[3]=s_lon;
	break;
      case 'e':
	l_time = strtol(optarg, NULL, 10);
	new_analysis_range[4]=l_time;
	break;
      case 'f':
	l_level = strtol(optarg, NULL, 10);
	new_analysis_range[5]=l_level;
	break;
      case 'g':
	l_lat = strtol(optarg, NULL, 10);
	new_analysis_range[6]=l_lat;
	break;
      case 'h':
	l_lon = strtol(optarg, NULL, 10);
	new_analysis_range[7]=l_lon;
	break;
      case 's':
	strncpy(SERVER_STRING,optarg,NAME_MAX);
	break;
      case 'i':
	io = strtol(optarg, NULL, 10);
      case 'j':
	db = strtol(optarg, NULL, 10);
      case 'k':
	total_entry = strtol(optarg, NULL, 10);
      case 'm':
	total_search = strtol(optarg, NULL, 10);
      case 'n':
	total_db = strtol(optarg, NULL, 10);
      default:
	break;
      }
}
