#include <iostream>
#include <string>
#include <vector>

#include "pwd.h"
#include "time.h"
#include "slurm/slurm.h"

#include "CLI11.hpp"


int main(int argc, char** argv) {

    // Define and set up the cli flags and options for controlling the printing
    CLI::App app{"A Slurm-compatible implementation of Maui's showq."};
    bool blocking = false, idle = false, running = false, completed = false, summary = false;
    std::string partition = "", reservation = "", username = "", where_clause = "";
    
    app.add_flag("-b,--blocking", blocking, "Show blocked jobs");
    app.add_flag("-i,--idle", idle, "Show idle jobs");
    app.add_flag("-r,--running", running, "Show running jobs");
    app.add_flag("-c,--completed", completed, "Show completed jobs");
    app.add_flag("-s,--summary", summary, "Show workload summary");
    app.add_option("-u,--username", username, "Show jobs for a specific user");
    app.add_option("-p,--partition", partition, "Show jobs for a specific partition");
    app.add_option("-R,--reservation", reservation, "Show jobs for a specific reservation");
    app.add_option("-w,--where", where_clause, "Show jobs for a specific partition");
    CLI11_PARSE(app, argc, argv);
    
    // Load partition, node, and job information
    partition_info_msg_t *part_buffer_ptr = NULL;
    node_info_msg_t *node_buffer_ptr = NULL;
    job_info_msg_t *job_buffer_ptr = NULL;
    if(slurm_load_partitions( (time_t) NULL, &part_buffer_ptr, SHOW_ALL)
            || slurm_load_node( (time_t) NULL, &node_buffer_ptr, SHOW_ALL)
            || slurm_load_jobs( (time_t) NULL, &job_buffer_ptr, SHOW_ALL) ) {
        std::cerr << "Unable to query Slurm information" << std::endl;
        exit(3);
    }
    
    // Sort jobs into running, idle, blocked, and complete
    std::vector<job_info_t *> jobs_running, jobs_idle, jobs_blocked, jobs_complete;
    for (int i = 0; i < job_buffer_ptr->record_count; i++) {
        job_info_t * job_ptr = &job_buffer_ptr->job_array[i];
        
        // If a filter is defined and doesn't hit, skip this job 
        struct passwd *pw = getpwuid(job_ptr->user_id);
        std::string job_user = (pw) ? std::string(pw->pw_name) : "";
        if (username != "" && username != job_user) continue;
        
        if (partition != "" && std::string(job_ptr->partition).find(partition) == std::string::npos) {
            continue;
        }
        
        if (reservation != "" && std::string(job_ptr->resv_name).find(reservation) == std::string::npos) {
            continue;
        }

        if (job_ptr->job_state == JOB_RUNNING) {
            jobs_running.push_back(job_ptr);
        } else if (job_ptr->job_state & JOB_COMPLETING) {
            jobs_complete.push_back(job_ptr);
        } else {
            if (job_ptr->state_reason == WAIT_DEPENDENCY
                    || job_ptr->state_reason == WAIT_HELD
                    || job_ptr->state_reason == WAIT_TIME           
                    || job_ptr->state_reason == WAIT_ASSOC_JOB_LIMIT          
                    || job_ptr->state_reason == WAIT_QOS_MAX_CPU_PER_JOB
                    || job_ptr->state_reason == WAIT_QOS_MAX_CPU_MINS_PER_JOB 
                    || job_ptr->state_reason == WAIT_QOS_MAX_NODE_PER_JOB
                    || job_ptr->state_reason == WAIT_QOS_MAX_WALL_PER_JOB
                    || job_ptr->state_reason == WAIT_HELD_USER) {
                jobs_blocked.push_back(job_ptr);
            } else {
                jobs_idle.push_back(job_ptr);
            }
        }
    }
    
    if (summary) {
        std::cout << "\nactive jobs: " << jobs_running.size() << "  eligible jobs: " 
            << jobs_idle.size() << "  blocked jobs: " << jobs_blocked.size() << "\n\nTotal jobs: "
            << jobs_running.size() + jobs_idle.size() + jobs_blocked.size() << "\n\n";
        return 0;
    }
    if (completed) {
        // I don't
    } 
    if (running) {
        // wanna
    } 
    if (idle) {
        // write
    } 
    if (blocking) {
        // these
    }

    // parts...
    return 0;
}
