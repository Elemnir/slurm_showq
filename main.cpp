#include <cmath>
#include <ctime>
#include <iostream>
#include <string>
#include <sstream>
#include <vector>

#include "grp.h"
#include "pwd.h"

#include "slurm/slurm.h"

#include "CLI11.hpp"


std::string uid2name(unsigned int uid) {
    passwd *pw = getpwuid(uid);
    return (pw) ? std::string(pw->pw_name) : std::to_string(uid);
}


std::string gid2name(unsigned int gid) {
    group *gp = getgrgid(gid);
    return (gp) ? std::string(gp->gr_name) : std::to_string(gid);
}

const char* state2cstr(unsigned int state) {
    switch(state) {
        case JOB_PENDING: return "Idle";
        case JOB_RUNNING: return "Running";
        case JOB_SUSPENDED: return "Suspended"; 
        case JOB_COMPLETE: return "Complete";
        case JOB_CANCELLED: return "Cancelled";
        case JOB_FAILED: return "Failed";
        case JOB_TIMEOUT: return "TimeOut";
        case JOB_NODE_FAIL: return "NodeFail";
        case JOB_PREEMPTED: return "Preempted";
        case JOB_BOOT_FAIL: return "BootFail";
        case JOB_DEADLINE: return "Deadline";
        case JOB_OOM: return "OomError";
        default: return "Unknown";
    }
}


std::string calc_duration( const time_t end, const time_t begin) {
    std::stringstream ss;
    int dur_sec = std::difftime(end, begin);
    int dur_min = dur_sec / 60;
    int dur_hrs = dur_min / 60;
    int dur_day = dur_hrs / 24;
    ss << dur_day << ':' << dur_hrs % 24 << ':' << dur_min % 60 << ':' << dur_sec % 60;
    return ss.str();
}


double calc_xfactor(job_info_t *j) {
    time_t until = (j->job_state == JOB_PENDING) ? std::time(nullptr) : j->start_time;
    return std::max(1.0, std::difftime(until, j->eligible_time) / j->time_limit);
}


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
    partition_info_msg_t *part_buffer_ptr = nullptr;
    node_info_msg_t *node_buffer_ptr = nullptr;
    job_info_msg_t *job_buffer_ptr = nullptr;
    if(slurm_load_partitions( (std::time_t) nullptr, &part_buffer_ptr, SHOW_ALL)
            || slurm_load_node( (std::time_t) nullptr, &node_buffer_ptr, SHOW_ALL)
            || slurm_load_jobs( (std::time_t) nullptr, &job_buffer_ptr, SHOW_ALL) ) {
        std::cerr << "Unable to query Slurm information" << std::endl;
        return 3;
    }
    
    // Filter and sort the jobs
    std::vector<job_info_t *> jobs_running, jobs_idle, jobs_blocked, jobs_complete;
    for (int i = 0; i < job_buffer_ptr->record_count; i++) {
        job_info_t * job_ptr = &job_buffer_ptr->job_array[i];
        
        // If a filter is defined and doesn't hit, skip this job 
        if (username != "" && username != uid2name(job_ptr->user_id)) { 
            continue;
        }
        
        if (partition != "" && std::string(job_ptr->partition).find(partition) == std::string::npos) {
            continue;
        }
        
        if (reservation != "" && std::string(job_ptr->resv_name).find(reservation) == std::string::npos) {
            continue;
        }

        // Sort jobs into running, idle, blocked, and completed
        if (job_ptr->job_state == JOB_RUNNING) {
            jobs_running.push_back(job_ptr);
        } else if (job_ptr->job_state & JOB_COMPLETING) {
            jobs_complete.push_back(job_ptr);
        } else if (job_ptr->state_reason == WAIT_DEPENDENCY
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
    
    // Print the requested report
    char part_buf[4] = "   ";
    if (summary) {
        std::cout << "\nactive jobs: " << jobs_running.size() << "  eligible jobs: " 
            << jobs_idle.size() << "  blocked jobs: " << jobs_blocked.size() << "\n\nTotal jobs: "
            << jobs_running.size() + jobs_idle.size() + jobs_blocked.size() << "\n\n";
        return 0;
    }
    if (completed) {
        printf("\n\ncompleted jobs---------------------\n");
        printf("%-19s %-10s %-6s %3s %7s %2s %9s %9s %16s %5s %11s %21s\n\n", 
            "JOBID", "STATUS", "CCODE", "PAR", "XFACTOR", "Q", "USERNAME", "GROUP", 
            "MHOST", "PROCS", "WALLTIME", "COMPLETIONTIME"
        );
        for (job_info_t* ji : jobs_complete) {
            printf("%-19d %-10s %-6d %3s %7.1f %2s %9s %9s %16s %5d %11s %21s\n", 
                ji->job_id, 
                state2cstr(ji->job_state), 
                ji->exit_code, 
                std::string(ji->partition).substr(0,3).c_str(), 
                calc_xfactor(ji),
                std::string(ji->qos).substr(0,2).c_str(), 
                uid2name(ji->user_id).c_str(), 
                gid2name(ji->group_id).c_str(), 
                ji->batch_host,
                ji->num_tasks,
                calc_duration(ji->end_time, ji->start_time).c_str(),
                std::ctime(&(ji->end_time))
            );
        }
        std::cout << "\ncompleted jobs: " << jobs_complete.size() << "\n\nTotal jobs: " 
            << jobs_complete.size() << "\n\n";
        return 0;
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
