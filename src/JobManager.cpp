#include <unistd.h>
#include <iostream>
#include <sstream>
#include <string>

#include "JobManager.h"
#include "B9CreatorSettings.h"
#include "DisplayManager.h"

int JobManager::loadJob(const std::string filename){
}

int JobManager::loadImg(const std::string filename){
}

int JobManager::initJob(bool withReset){
	m_job_mutex.lock();
	if( m_state != IDLE && m_state != START_STATE ){
		std::string msg("(Job) Job already running. Abort initialisation of job.");
		std::cerr << msg << std::endl;
		m_b9CreatorSettings.m_queues.add_message(msg);
		return -1;
	}

	// reset at least one time
	if( withReset ||
			( (m_b9CreatorSettings.m_resetStatus != 0) && m_state == START_STATE )
		){
		m_state = RESET;
	}else{
		m_state = INIT;
	}
	m_job_mutex.unlock();

return 0;	
}

int JobManager::startJob(){
	if( m_state == START_STATE ){
	}
	
	if( m_state != IDLE ){
		std::string msg("(Job) Can not start job.");
		std::cerr << msg << std::endl;
		m_b9CreatorSettings.m_queues.add_message(msg);
		return -1;
	}

	m_b9CreatorSettings.lock();
	m_b9CreatorSettings.m_printProp.m_lockTimes = true;
	m_b9CreatorSettings.unlock();

	m_state = FIRST_LAYER;
	return 0;
}

int JobManager::pauseJob(){
	if( m_state == IDLE ) return -1;

	Messages &q = m_b9CreatorSettings.m_queues;

	gettimeofday( &m_tTimer.begin, NULL );
	//m_tPause.begin = (timeval_t*) time(NULL);
	m_pauseInState = m_state;

	/** Try to reach hazard-free state. **/
	/* Blank image. */
	m_displayManager.blank();

	/* Close VAT if printer is equipped with slider */
	if( m_b9CreatorSettings.m_shutterEquipped ){
		std::string cmd_close("V0"); 
		q.add_command(cmd_close);	
		m_b9CreatorSettings.m_queues.add_command(cmd_close);	
	}

	m_state = PAUSE;
	return 0;
}

int JobManager::resumeJob(){
	if( m_state != PAUSE ) return -1;

	Messages &q = m_b9CreatorSettings.m_queues;
	// Analyse m_pauseInState to fix some timing
	switch( m_pauseInState ){
		case IDLE:
			{ 
				std::string msg("Can not resume job. It was not paused");
				std::cerr << msg << std::endl;
				m_b9CreatorSettings.m_queues.add_message(msg);
				return -1;
			}
			break;
		case BREATH:
			{
				// Open shutter 
				if( m_b9CreatorSettings.m_shutterEquipped ){
					std::string cmd_close("V0"); 
					q.add_command(cmd_close);	
					m_b9CreatorSettings.m_queues.add_command(cmd_close);	
				}
				VPRINT("Repeat breath for layer %i.\n",
						m_b9CreatorSettings.m_printProp.m_currentLayer);
				gettimeofday( &m_tBreath.begin, NULL );
			}
			break;
		case CURING:
			{
				// Substract elapsed time before job was stoped.
				m_tCuring.diff = m_tCuring.diff
					- Timer::timeval_diff( &m_tPause.begin ,&m_tCuring.begin );
				gettimeofday( &m_tCuring.begin, NULL );
				m_displayManager.show(); //TODO
			}
			break;
		case WAIT_ON_R_MESS:
			{
				m_pauseInState = RESET;
			}
			break;
		case WAIT_ON_F_MESS:
			{
				m_b9CreatorSettings.lock();
				m_b9CreatorSettings.m_readyForNextCycle = true;
				m_b9CreatorSettings.unlock();
			}
			break;
		default:
			break;
	}

	m_state = m_pauseInState;
	m_pauseInState = IDLE;
	return 0;
}

int JobManager::stopJob(){
	//can not stop job, if in idle state
	if( m_state == IDLE) return -1;

	// reset pause state to default value
	if( m_state == PAUSE ) m_pauseInState = IDLE;

	m_state = FINISH;
	return 0;
}

/*
int JobManager::nextStep(){
	m_state = NEXT_LAYER;
	return 0;
}*/

void JobManager::run(){
	Messages &q = m_b9CreatorSettings.m_queues;

	while( !m_b9CreatorSettings.m_die && !m_die ){
		m_job_mutex.lock();
		switch( m_state ){
			case RESET:
				{
					gettimeofday( &m_tRWait.begin, NULL );
					m_tRWait.diff = MaxWaitR; 

					/* Send reset command to printer */
					std::string cmd_reset("R"); 
					q.add_command(cmd_reset);	

					VPRINT("Reset requested. Now wait on 'R' Message.\n");
					m_state = WAIT_ON_R_MESS;
				}
				break;
			case WAIT_ON_R_MESS:
				{
					VPRINT("Wait on 'R' message...\n");
					if( m_b9CreatorSettings.m_resetStatus == 0  ){
						m_state = INIT;
					}
					if( m_tRWait.timePassed() ){
						std::string msg("(Job) Wait to long on ready signal of printer. Printer connection lost?");
						std::cerr << msg << std::endl;
						m_b9CreatorSettings.m_queues.add_message(msg);
						//m_state = IDLE;
m_b9CreatorSettings.m_resetStatus = 0;//test
					}
					if( m_b9CreatorSettings.m_die ){
						m_state = IDLE;
					}
				}
				break;
			case INIT:
				{
					/* Update machine data ('A' covers 'I' command.) */
					std::string cmd_info("A"); 
					q.add_command(cmd_info);	

					// Set layer number to base layer index.
					m_b9CreatorSettings.m_printProp.m_currentLayer = 1;

					// reset pause state to default value
					m_pauseInState = IDLE;

					VPRINT("Init done. Idle in JobManager.\n");

					//m_state = FIRST_LAYER;
					m_state = IDLE;
				}
				break;
			case FIRST_LAYER:
				{
					std::string cmd_base("B0"); 
					q.add_command(cmd_base);	

					//wait on shutter opening.
					/* Thats not possible. 'F' message is send on shutter opening and not
					 * after table movement.
					VPRINT("First layer state. Wait on 'F' message\n");
					gettimeofday( &(m_tFWait.begin), NULL );
					m_tFWait.diff = MaxWaitFfrist; 
					m_state = WAIT_ON_F_MESS;
					*/

					/* Wait on zHeight = 0 */
					gettimeofday( &(m_tFWait.begin), NULL );
					m_tFWait.diff = MaxWaitF; 
					VPRINT("Move Table to base position.\n");

					m_state = WAIT_ON_ZERO_HEIGHT;

				}
				break;
			case WAIT_ON_ZERO_HEIGHT:
				{
					if( m_b9CreatorSettings.m_zHeight == 0 ){
						m_state = WAIT_ON_F_MESS; // or BREATH
					}else{
						VPRINT("Wait till base position reached...\n");
					}
				}
				break;
			case NEXT_LAYER:
				{
					int &zHeight = m_b9CreatorSettings.m_zHeight; //Reference!
					int zHeightLimit = m_b9CreatorSettings.m_zHeightLimit; 
					int zRes = m_b9CreatorSettings.m_printProp.m_zResolution;
					int pu = m_b9CreatorSettings.m_PU;
					int l = m_b9CreatorSettings.m_printProp.m_currentLayer;

					//int zHeight2 = 100*( zRes )/pu + zHeight; //sum up rounding errors
					int zHeight2 = 100*( (l-1)*zRes )/pu; //require zHeight(layer 1)=0
					if( zHeight2 > zHeightLimit ){
						std::ostringstream zError;
						zError << "(Job) Height of next layer lower as current height. Abort job."
						<< std::endl << "current height: " << zHeight << " Next height: " << zHeight2
						<< std::endl << "Height limit: " << zHeightLimit
						<< std::endl << "Next layer: " << l;
						std::string zErrorStr = zError.str();
						VPRINT("%s", zErrorStr.c_str() );
						q.add_message( zErrorStr );	
						m_state = ERROR;
						break;
					}
					if( zHeight2 <= zHeight ){
						std::ostringstream zError;
						zError << "(Job) Height of next layer lower as current height. Abort job."
						<< std::endl << "current height: " << zHeight << " Next height: " << zHeight2
						<< std::endl << "Next layer: " << l;
						std::string zErrorStr = zError.str();
						VPRINT("%s", zErrorStr.c_str() );
						q.add_message( zErrorStr );	
						m_state = ERROR;
						break;
					}

					//update height.
					//zHeight = zHeight2;
					//no, do not update value manually. wait on message on serial channel.

					std::ostringstream cmd_next;
					cmd_next << "N" << zHeight2 ;
					std::string cmd_nextStr = cmd_next.str();
					q.add_command( cmd_nextStr );	
					VPRINT("Next layer state. Send N%i for next layer.\n", zHeight2 );

					//wait on shutter opening.
					gettimeofday( &m_tFWait.begin, NULL );
					m_tFWait.diff = MaxWaitF; 
					m_state = WAIT_ON_F_MESS;
				}
				break;
				/*		case LAST_LAYER:
							{
							}
							break;*/
			case WAIT_ON_F_MESS:
				{	
					VPRINT("Wait on 'F' message...\n");
					bool & r = m_b9CreatorSettings.m_readyForNextCycle;
					if( r || m_tFWait.timePassed() ){
					//if( r ){
						// unset the ready flag
						r = false;

						gettimeofday( &m_tBreath.begin, NULL );
						m_tBreath.diff = m_b9CreatorSettings.m_printProp.m_breathTime*1000000;

						VPRINT("Begin breath for layer %i.\n",
								m_b9CreatorSettings.m_printProp.m_currentLayer);
						m_state = BREATH;
					}else
					if( m_tFWait.timePassed() ){
						std::string msg("(Job) Wait to long on 'F' signal of printer. Printer connection lost?");
						std::cerr << msg << std::endl;
						m_b9CreatorSettings.m_queues.add_message(msg);
						m_state = IDLE;
					}
				}
				break;
			case BREATH:
				{
					VPRINT("Breathing...\n");
					if( m_tBreath.timePassed() ){
						int l = m_b9CreatorSettings.m_printProp.m_currentLayer;
						gettimeofday( &m_tCuring.begin, NULL );
						if( l <= m_b9CreatorSettings.m_printProp.m_nmbrOfAttachedLayers ){
							m_tCuring.diff = m_b9CreatorSettings.m_printProp.m_exposureTimeAL*1000000;
						}else{
							m_tCuring.diff = m_b9CreatorSettings.m_printProp.m_exposureTime*1000000;
						}

						m_displayManager.show(); //TODO
						m_state = CURING;
						VPRINT("Start curing of layer %i with %is.\n",l, (int) (m_tCuring.diff/1000000) );
					}
				}
				break;
			case CURING:
				{
					VPRINT("Curing...\n");
					if( m_tCuring.timePassed() ){
						//hide slice on projector image.
						m_displayManager.blank();

						int &l = m_b9CreatorSettings.m_printProp.m_currentLayer;
						l++;
						if( l <= m_b9CreatorSettings.m_printProp.m_maxLayer ){
							m_state = NEXT_LAYER;
						}else {
							m_state = FINISH;
						}

					}
				}
				break;
			case PAUSE:
				{
					VPRINT("Pause...\n");
				}
				break;
			case ERROR:
			case FINISH:
				{
					m_displayManager.blank();
					//TODO?! Power of Projector?!

					std::string cmd_finished;
					VPRINT("Send F%i. Job finished\n",9000 );
					cmd_finished = "F9000" ; 
					q.add_command(cmd_finished);

					m_b9CreatorSettings.lock();
					m_b9CreatorSettings.m_printProp.m_lockTimes = false;
					m_b9CreatorSettings.unlock();

					m_state = IDLE;
				}
				break;
		case IDLE:
				{
					VPRINT("Idle...\n");
				}
				break;
		case START_STATE:
				{

				}
				break;
			default: 
				std::cout << "State " << m_state << " unknown" << std::endl;
		}
		m_b9CreatorSettings.lock();
		m_b9CreatorSettings.m_jobState = m_state;
		m_b9CreatorSettings.unlock();

		m_job_mutex.unlock();
		usleep(50000); //.05s
		usleep(2000000); //2s
	}
}

void JobManager::webserverSetState(onion_request *req, int actionid, std::string &reply){
	
	reply = "error";

	if(actionid == 6){ /* control JobManager */
		std::string print_cmd ( onion_request_get_post(req,"print") );
#ifdef VERBOSE
		std::cout << "'"<< print_cmd << "'" << std::endl;
#endif
		if( 0 == print_cmd.compare("init") ){
			if( 0 != initJob( (m_b9CreatorSettings.m_resetStatus != 0)  ) ) return ;
			reply = "idle";

		}else if( 0 == print_cmd.compare("start") || 0 == print_cmd.compare("toggle")){
			if( m_state == START_STATE){
				//we can not start job. Init at first.
				if( 0 != initJob( (m_b9CreatorSettings.m_resetStatus != 0)  ) ) return ;
				reply = "idle";
				return;
			}
			if( m_state == IDLE ){
				if( 0 != startJob() ) return ;
				reply = "print";
			}
		}else if( 0 == print_cmd.compare("pause") || 0 == print_cmd.compare("toggle") ){
			if( m_state == PAUSE ){
				if( 0 != pauseJob() ) return ;
				reply = "pause";
			}
			//can not resume without pause state.
		}else if( 0 == print_cmd.compare("resume") ){
			if( 0 != resumeJob() ) return  ;
			reply = "print";

		}else if( 0 == print_cmd.compare("abort") ){
			if( 0 != stopJob() ){
				if( m_state == IDLE ){
					//stop failed, but printer mode is idle. Thus,
					//stopping produce no error.
					reply = "idle";
				}
				return  ;
			}
			reply = "idle";
		}

		//print_cmd unknown
	}
}
