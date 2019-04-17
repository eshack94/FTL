/* Pi-hole: A black hole for Internet advertisements
*  (c) 2017 Pi-hole, LLC (https://pi-hole.net)
*  Network-wide ad blocking via your own hardware.
*
*  FTL Engine
*  Garbage collection routines
*
*  This file is copyright under the latest version of the EUPL.
*  Please see LICENSE file for your rights under this license. */

#include "FTL.h"
#include "shmem.h"

bool doGC = false;

time_t lastGCrun = 0;
void *GC_thread(void *val)
{
	// Set thread name
	prctl(PR_SET_NAME,"housekeeper",0,0,0);

	// Save timestamp as we do not want to store immediately
	// to the database
	lastGCrun = time(NULL) - time(NULL)%GCinterval;
	while(!killed)
	{
		if(time(NULL) - GCdelay - lastGCrun >= GCinterval || doGC)
		{
			doGC = false;
			// Update lastGCrun timer
			lastGCrun = time(NULL) - GCdelay - (time(NULL) - GCdelay)%GCinterval;

			// Lock FTL's data structure, since it is likely that it will be changed here
			// Requests should not be processed/answered when data is about to change
			lock_shm();

			// Get minimum time stamp to keep
			time_t mintime = (time(NULL) - GCdelay) - MAXLOGAGE*3600;

			// Align to the start of the next hour. This will also align with
			// the oldest overTime interval after GC is done.
			mintime -= mintime % 3600;
			mintime += 3600;

			if(config.debug & DEBUG_GC) timer_start(GC_TIMER);

			int removed = 0;
			if(config.debug & DEBUG_GC) logg("GC starting, mintime: %lu %s", mintime, ctime(&mintime));

			// Process all queries
			for(long int i=0; i < counters->queries; i++)
			{
				queriesData* query = getQuery(i, true);
				// Test if this query is too new
				if(query->timestamp > mintime)
					break;

				// Adjust client counter
				clientsData* client = getClient(query->clientID, true);
				client->count--;

				// Adjust total counters and total over time data
				const int timeidx = query->timeidx;
				overTime[timeidx].total--;
				// Adjust corresponding overTime counters
				client->overTime[timeidx]--;

				// Adjust domain counter (no overTime information)
				domainsData* domain = getDomain(query->domainID, true);
				domain->count--;

				// Get forward pointer
				forwardedData* forward = getForward(query->forwardID, true);

				// Change other counters according to status of this query
				switch(query->status)
				{
					case QUERY_UNKNOWN:
						// Unknown (?)
						counters->unknown--;
						break;
					case QUERY_FORWARDED:
						// Forwarded to an upstream DNS server
						// Adjust counters
						counters->forwardedqueries--;
						forward->count--;
						overTime[timeidx].forwarded--;
						break;
					case QUERY_CACHE:
						// Answered from local cache _or_ local config
						counters->cached--;
						overTime[timeidx].cached--;
						break;
					case QUERY_GRAVITY: // Blocked by Pi-hole's blocking lists (fall through)
					case QUERY_BLACKLIST: // Exact blocked (fall through)
					case QUERY_WILDCARD: // Regex blocked (fall through)
					case QUERY_EXTERNAL_BLOCKED_IP: // Blocked by upstream provider (fall through)
					case QUERY_EXTERNAL_BLOCKED_NXRA: // Blocked by upstream provider (fall through)
					case QUERY_EXTERNAL_BLOCKED_NULL: // Blocked by upstream provider (fall through)
						counters->blocked--;
						overTime[timeidx].blocked--;
						domain->blockedcount--;
						client->blockedcount--;
						break;
					default:
						/* That cannot happen */
						break;
				}

				// Update reply counters
				switch(query->reply)
				{
					case REPLY_NODATA: // NODATA(-IPv6)
					counters->reply_NODATA--;
					break;

					case REPLY_NXDOMAIN: // NXDOMAIN
					counters->reply_NXDOMAIN--;
					break;

					case REPLY_CNAME: // <CNAME>
					counters->reply_CNAME--;
					break;

					case REPLY_IP: // valid IP
					counters->reply_IP--;
					break;

					case REPLY_DOMAIN: // reverse lookup
					counters->reply_domain--;
					break;

					default: // Incomplete query or TXT, do nothing
					break;
				}

				// Update type counters
				if(query->type >= TYPE_A && query->type < TYPE_MAX)
				{
					counters->querytype[query->type-1]--;
					overTime[timeidx].querytypedata[query->type-1]--;
				}

				// Count removed queries
				removed++;

			}

			// Only perform memory operations when we actually removed queries
			if(removed > 0)
			{
				// Move memory forward to keep only what we want
				// Note: for overlapping memory blocks, memmove() is a safer approach than memcpy()
				// Example: (I = now invalid, X = still valid queries, F = free space)
				//   Before: IIIIIIXXXXFF
				//   After:  XXXXFFFFFFFF
				memmove(getQuery(0, true), getQuery(removed, true), (counters->queries - removed)*sizeof(queriesData));

				// Update queries counter
				counters->queries -= removed;
				// Update DB index as total number of queries reduced
				lastdbindex -= removed;

				// ensure remaining memory is zeroed out (marked as "F" in the above example)
				memset(getQuery(counters->queries, true), 0, (counters->queries_MAX - counters->queries)*sizeof(queriesData));
			}

			// Determine if overTime memory needs to get moved
			moveOverTimeMemory(mintime);

			if(config.debug & DEBUG_GC)
				logg("Notice: GC removed %i queries (took %.2f ms)", removed, timer_elapsed_msec(GC_TIMER));

			// Release thread lock
			unlock_shm();

			// After storing data in the database for the next time,
			// we should scan for old entries, which will then be deleted
			// to free up pages in the database and prevent it from growing
			// ever larger and larger
			DBdeleteoldqueries = true;
		}
		sleepms(100);
	}

	return NULL;
}
