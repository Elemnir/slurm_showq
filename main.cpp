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


std::string timestamp2str(time_t t) {
    std::string raw(std::ctime(&t));
    return raw.substr(0, raw.size() - 6);
}


std::string duration2str(int dur_sec) {
    std::stringstream ss;
    if (dur_sec < 0) {
        dur_sec = -dur_sec;
        ss << '-';
    }
    if (dur_sec / 60 / 60 / 24 > 0) {
        ss << (dur_sec / 60 / 60 / 24) << ':' << std::setw(2);
    }
    ss << (dur_sec / 60 / 60) % 24 << ':'
       << std::setw(2) << std::setfill('0') << (dur_sec / 60) % 60 << ':' 
       << std::setw(2) << std::setfill('0') << (dur_sec) % 60;
    return ss.str();
}


double calc_xfactor(job_info_t *j) {
    time_t until = (j->job_state == JOB_PENDING) ? std::time(nullptr) : j->start_time;
    return std::max(1.0, std::difftime(until, j->eligible_time) / (j->time_limit * 60));
}


int main(int argc, char** argv) {

    // Define and set up the cli flags and options for controlling the printing
    CLI::App app{"A Slurm-compatible implementation of Maui's showq."};
    bool blocking = false, idle = false, running = false, completed = false, summary = false;
    std::string partition, reservation, username, groupname, account, qosname;
    
    app.add_flag("-b,--blocking", blocking, "Show blocked jobs");
    app.add_flag("-i,--idle", idle, "Show idle jobs");
    app.add_flag("-r,--running", running, "Show running jobs");
    app.add_flag("-c,--completed", completed, "Show completed jobs");
    app.add_flag("-s,--summary", summary, "Show workload summary");
    app.add_option("-u,--username", username, "Show jobs for a specific user");
    app.add_option("-g,--group", groupname, "Show jobs for a specific group");
    app.add_option("-a,--account", account, "Show jobs for a specific account");
    app.add_option("-p,--partition", partition, "Show jobs for a specific partition");
    app.add_option("-q,--qos", qosname, "Show jobs for a specific QoS");
    app.add_option("-R,--reservation", reservation, "Show jobs for a specific reservation");
    //app.add_option("-w,--where", where_clause, "");
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
    hostlist_t running_nodes = slurm_hostlist_create("");
    for (int i = 0; i < job_buffer_ptr->record_count; i++) {
        job_info_t * job_ptr = &job_buffer_ptr->job_array[i];
        
        // If a filter is defined and doesn't hit, skip this job 
        if (username != "" && username != uid2name(job_ptr->user_id)) continue;
        if (groupname != "" && groupname != gid2name(job_ptr->group_id)) continue;
        if (account != "" && account != std::string(job_ptr->account)) continue;
        if (qosname != "" && qosname != std::string(job_ptr->qos)) continue;
        
        if (partition != "" && std::string(job_ptr->partition).find(partition) == std::string::npos) {
            continue;
        }
        
        if (reservation != "" && std::string(job_ptr->resv_name).find(reservation) == std::string::npos) {
            continue;
        }

        // Sort jobs into running, idle, blocked, and completed
        if (job_ptr->job_state == JOB_RUNNING) {
            jobs_running.push_back(job_ptr);
            slurm_hostlist_push(running_nodes, job_ptr->nodes);
        } else if (job_ptr->job_state == JOB_PENDING) {
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
        } else {
            jobs_complete.push_back(job_ptr);
        }
    }
    
    // Collect nodes in the relevant partition(s) for utilization stats
    hostlist_t partition_nodes = slurm_hostlist_create("");
    for (int i = 0; i < part_buffer_ptr->record_count; i++) {
        partition_info_t *part_ptr = &part_buffer_ptr->partition_array[i];
        if (partition != "" && std::string(part_ptr->name).find(partition) == std::string::npos) {
            continue;
        }
        slurm_hostlist_push(partition_nodes, part_ptr->nodes);
    }

    slurm_hostlist_uniq(running_nodes);
    slurm_hostlist_uniq(partition_nodes);
    int running_nodes_count = slurm_hostlist_count(running_nodes);
    int partition_nodes_count = slurm_hostlist_count(partition_nodes);

    // Print the requested report
    if (summary) {
        std::cout << "\nactive jobs: " << jobs_running.size() << "  eligible jobs: " 
            << jobs_idle.size() << "  blocked jobs: " << jobs_blocked.size() << "\n\nTotal jobs: "
            << jobs_running.size() + jobs_idle.size() + jobs_blocked.size() << "\n\n";
        return 0;
    }
    if (completed) {
        printf("\ncompleted jobs---------------------\n");
        printf("%-19s %-10s %-6s %3s %7s %2s %9s %9s %16s %5s %11s  %21s\n\n", 
            "JOBID", "STATUS", "CCODE", "PAR", "XFACTOR", "Q", "USERNAME", "GROUP", 
            "MHOST", "PROCS", "WALLTIME", "COMPLETIONTIME"
        );
        for (job_info_t* ji : jobs_complete) {
            printf("%-19u %-10s %-6u %3s %7.1f %2s %9s %9s %16s %5u %11s  %21s\n", 
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
                duration2str(std::difftime(ji->end_time, ji->start_time)).c_str(),
                timestamp2str(ji->end_time).c_str()
            );
        }
        std::cout << '\n' << jobs_complete.size() << " completed jobs\n\nTotal jobs: " 
            << jobs_complete.size() << "\n\n";
        return 0;
    } 
    if (running) {
        printf("\nactive jobs------------------------\n");
        printf("%-19s %-10s %3s %7s %2s %9s %9s %16s %5s %11s  %21s\n\n", 
            "JOBID", "STATUS", "PAR", "XFACTOR", "Q", "USERNAME", "GROUP", 
            "MHOST", "PROCS", "REMAINING", "STARTTIME"
        );
        for (job_info_t* ji : jobs_running) {
            printf("%-19u %-10s %3s %7.1f %2s %9s %9s %16s %5u %11s  %21s\n", 
                ji->job_id, 
                state2cstr(ji->job_state),
                std::string(ji->partition).substr(0,3).c_str(), 
                calc_xfactor(ji),
                std::string(ji->qos).substr(0,2).c_str(), 
                uid2name(ji->user_id).c_str(), 
                gid2name(ji->group_id).c_str(), 
                ji->batch_host,
                ji->num_tasks,
                duration2str(std::difftime(ji->end_time, std::time(nullptr))).c_str(),
                timestamp2str(ji->start_time).c_str()
            );
        }
        std::cout << '\n' << jobs_running.size() << " active jobs\t\t" << running_nodes_count
            << " of " << partition_nodes_count << " nodes active      (" << std::setprecision(2)
            << static_cast<double>(running_nodes_count) / partition_nodes_count * 100 << "%)"
            << "\n\nTotal jobs: " << jobs_running.size() << "\n\n";
        return 0;
    } 
    if (idle) {
        printf("\neligible jobs----------------------\n");
        printf("%-19s %10s %3s %7s %2s %9s %9s %5s %11s  %21s\n\n", 
            "JOBID", "PRIORITY", "PAR", "XFACTOR", "Q", "USERNAME", "GROUP", 
            "PROCS", "WCLIMIT", "SYSTEMQUEUETIME"
        );
        for (job_info_t* ji : jobs_idle) {
            printf("%-19u %10u %3s %7.1f %2s %9s %9s %5u %11s  %21s\n", 
                ji->job_id, 
                ji->priority,  
                std::string(ji->partition).substr(0,3).c_str(), 
                calc_xfactor(ji),
                std::string(ji->qos).substr(0,2).c_str(), 
                uid2name(ji->user_id).c_str(), 
                gid2name(ji->group_id).c_str(), 
                ji->num_tasks,
                duration2str(ji->time_limit * 60).c_str(),
                timestamp2str(ji->submit_time).c_str()
            );
        }
        std::cout << '\n' << jobs_idle.size() << " eligible jobs\n\nTotal jobs: " 
            << jobs_idle.size() << "\n\n";
        return 0;

    } 
    if (blocking) {
        printf("\nblocked jobs-----------------------\n");
        printf("%-18s %8s %8s %10s %5s %11s  %21s\n\n",
            "JOBID", "USERNAME", "GROUP", "STATE", "PROCS", "WCLIMIT", "QUEUETIME"
        );
        for (job_info_t *ji : jobs_blocked) {
            printf("%-18u %8s %8s %10s %5u %11s  %21s\n",
                ji->job_id,
                uid2name(ji->user_id).c_str(),
                gid2name(ji->group_id).c_str(),
                state2cstr(ji->job_state),
                ji->num_tasks,
                duration2str(ji->time_limit * 60).c_str(),
                timestamp2str(ji->submit_time).c_str()
            );
        }
        std::cout << '\n' << jobs_blocked.size() << " blocked jobs\n\nTotal jobs: " 
            << jobs_blocked.size() << "\n\n";
        return 0;

    }
    
    printf("\nactive jobs------------------------\n");
    printf("%-18s %8s %10s %5s %11s  %21s\n\n", 
        "JOBID", "USERNAME", "STATE", "PROCS", "REMAINING", "STARTTIME"
    ); 
    for (job_info_t *ji : jobs_running) {
        printf("%-18u %8s %10s %5u %11s  %21s\n", 
            ji->job_id, 
            uid2name(ji->user_id).c_str(),
            state2cstr(ji->job_state),
            ji->num_tasks,
            duration2str(std::difftime(ji->end_time, std::time(nullptr))).c_str(),
            timestamp2str(ji->start_time).c_str()
        );
    }
    std::cout << '\n' << jobs_running.size() << " active jobs\t\t" << running_nodes_count
            << " of " << partition_nodes_count << " nodes active      (" << std::setprecision(2)
            << static_cast<double>(running_nodes_count) / partition_nodes_count * 100 << "%)";

    
    printf("\n\neligible jobs----------------------\n");
    printf("%-18s %8s %10s %5s %11s  %21s\n\n", 
        "JOBID", "USERNAME", "STATE", "PROCS", "WCLIMIT", "QUEUETIME"
    ); 
    for (job_info_t *ji : jobs_idle) {
        printf("%-18u %8s %10s %5u %11s  %21s\n", 
            ji->job_id, 
            uid2name(ji->user_id).c_str(),
            state2cstr(ji->job_state),
            ji->num_tasks,
            duration2str(ji->time_limit * 60).c_str(),
            timestamp2str(ji->submit_time).c_str()
        );
    }
    std::cout << '\n' << jobs_idle.size() << " eligible jobs";

    printf("\n\nblocked jobs-----------------------\n");
    printf("%-18s %8s %10s %5s %11s  %21s\n\n", 
        "JOBID", "USERNAME", "STATE", "PROCS", "WCLIMIT", "QUEUETIME"
    ); 
    for (job_info_t *ji : jobs_blocked) {
        printf("%-18u %8s %10s %5u %11s  %21s\n", 
            ji->job_id, 
            uid2name(ji->user_id).c_str(),
            state2cstr(ji->job_state),
            ji->num_tasks,
            duration2str(ji->time_limit * 60).c_str(),
            timestamp2str(ji->submit_time).c_str()
        );
    }
    std::cout << '\n' << jobs_blocked.size() << " blocked jobs\n\nTotal jobs: " 
        << jobs_blocked.size() + jobs_idle.size() + jobs_running.size() << "\n\n";
    return 0;
}
