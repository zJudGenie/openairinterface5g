//
// Created by Eugenio Moro on 04/24/23.
// Edited by Rey P. on 08/11/25.
//

#include "e2_message_handlers.h"

extern RAN_CONTEXT_t RC;
int gnb_id = 0;

/*
this function has not been implemented and it won't be needed in the foreseeable future
*/
void handle_subscription(RANMessage* in_mess){
  printf("Not implemented\n");
  ran_message__free_unpacked(in_mess,NULL);
  assert(0!=0);
}
/*
this function just basically prints out the parameters in the request and passes the in_mess to the response generator
*/
void handle_indication_request(RANMessage* in_mess,int out_socket, sockaddr_in peeraddr){
  printf("Indication request for %lu parameters:\n", in_mess->ran_indication_request->n_target_params);
  for(int par_i=0; par_i<in_mess->ran_indication_request->n_target_params; par_i++){
    printf("\tParameter id %d requested (a.k.a %s)\n",\
        in_mess->ran_indication_request->target_params[par_i],\
        get_enum_name(in_mess->ran_indication_request->target_params[par_i]));
  }
  build_indication_response(in_mess, out_socket, peeraddr);
}

/*
this function builds and sends the indication response based on the map inside the in_mess
in_mess is cleared here
*/
void build_indication_response(RANMessage* in_mess, int out_socket, sockaddr_in servaddr){

  RANIndicationResponse rsp = RAN_INDICATION_RESPONSE__INIT;
  RANParamMapEntry **map;
  void* buf;
  unsigned buflen, i;

  // allocate space for the pointers inside the map, which is NULL terminated so it needs 1 additional last pointer
  map = malloc(sizeof(RANParamMapEntry*) * (in_mess->ran_indication_request->n_target_params + 1));

  // now build every element inside the map
  for(i=0; i<in_mess->ran_indication_request->n_target_params; i++){

    // allocate space for this entry and initialize
    map[i] = malloc(sizeof(RANParamMapEntry));
    ran_param_map_entry__init(map[i]);

    // assign key
    map[i]->key=in_mess->ran_indication_request->target_params[i];

    // read the parameter and save it in the map
    ran_read(map[i]->key, map[i]);
  }
  // the map is ready, add the null terminator
  map[in_mess->ran_indication_request->n_target_params] = NULL;

  rsp.n_param_map=in_mess->ran_indication_request->n_target_params;
  rsp.param_map=map;
  buflen = ran_indication_response__get_packed_size(&rsp);
  buf = malloc(buflen);
  ran_indication_response__pack(&rsp,buf);

  printf("Sending indication response\n");
  unsigned slen = sizeof(servaddr);
  int rev = sendto(out_socket, (const char *)buf, buflen,
                   MSG_CONFIRM, (const struct sockaddr *) &servaddr,
                   slen);
  printf("Sent %d bytes, buflen was %u\n",rev, buflen);

  // free map and buffer (rsp not freed because in the stack)
  free_ran_param_map(map);
  free(buf);
  // free incoming ran message
  ran_message__free_unpacked(in_mess,NULL);
}

/*
this function frees a map through introspection, maps !!MUST!! be NULL terminated
*/
void free_ran_param_map(RANParamMapEntry **map){
  int i = 0;
  while(map[i] != NULL){
    // we first need to clear whatever is inside the map entry, we need to consider all the possible value types
    switch(map[i]->value_case){
      case RAN_PARAM_MAP_ENTRY__VALUE_INT64_VALUE:
        // there is no pointer inside the entry to free in this case
        break;
      case RAN_PARAM_MAP_ENTRY__VALUE_STRING_VALUE:
        // free the string and then the entry
        free(map[i]->string_value);
        break;
      case RAN_PARAM_MAP_ENTRY__VALUE_UE_LIST:
        // in this case we free the ue list first
        free_ue_list(map[i]->ue_list);
        break;
      case RAN_PARAM_MAP_ENTRY__VALUE__NOT_SET:
        // nothing to do here, skip to default
      default:
        break;
    }
    // now we can free the entry
    free(map[i]);
    i++;
  }
}

void handle_control(RANMessage* in_mess){
  // loop tarhet params and apply
  for(int i=0; i<in_mess->ran_control_request->n_target_param_map; i++){
    printf("Applying target parameter %s with value %s\n",\
        get_enum_name(in_mess->ran_control_request->target_param_map[i]->key),\
        in_mess->ran_control_request->target_param_map[i]->string_value);
    ran_write(in_mess->ran_control_request->target_param_map[i]);
  }
  // free incoming ran message
  ran_message__free_unpacked(in_mess,NULL);
}

const char* get_enum_name(RANParameter ran_par_enum){
  switch (ran_par_enum)
  {
    case RAN_PARAMETER__GNB_ID:
      return "gnb_id";
    case RAN_PARAMETER__UE_LIST:
      return "ue_list";
    default:
      return "unrecognized param";
  }
}

void ran_write(RANParamMapEntry* target_param_map_entry){
  switch (target_param_map_entry->key)
  {
    case RAN_PARAMETER__GNB_ID:
      gnb_id = atoi(target_param_map_entry->string_value);
      break;
    case RAN_PARAMETER__UE_LIST: // if we receive a ue list message we need to apply its content
      apply_properties_to_ue_list(target_param_map_entry->ue_list);
      break;
    default:
      printf("ERROR: cannot write RAN, unrecognized target param %d\n", target_param_map_entry->key);
  }
}

void apply_properties_to_ue_list(UeListM* ue_list){
  // loop the ues and apply what needed to each, according to what is inside the list received from the xapp
  for(int ue = 0; ue < ue_list->n_ue_info; ue++){
    // apply received properties
    set_ue_properties(ue_list->ue_info[ue]);
  }
}

void set_ue_properties(UeInfoM* ue_info){

  // iterate ue list until rnti is found
  bool rnti_not_found = true;
  NR_UEs_t *UEs = &RC.nrmac[0]->UE_info;
  UE_iterator(UEs->list, UE) {
    if (UE->rnti == ue_info->rnti) {
      printf("RNTI found\n");

      if (ue_info->has_ue_mcs_downlink) {
        // set MCS of PDSCH
        UE->UE_sched_ctrl.sched_pdsch.mcs = ue_info->ue_mcs_downlink;

        printf("INFO: MCS[%u] set to %d\n", ue_info->rnti, ue_info->ue_mcs_downlink);
      }

      rnti_not_found = false;
      break;
    }
  }

  if(rnti_not_found){
    printf("RNTI %u not found\n", ue_info->rnti);
  }
}

char* int_to_charray(int i){
  int length = (snprintf(NULL, 0,"%d",i)+1);
  char* ret = malloc(length*sizeof(char));
  sprintf(ret, "%d", i);
  return ret;
}

void handle_master_message(void* buf, int buflen, int out_socket, struct sockaddr_in servaddr){
  RANMessage* in_mess = ran_message__unpack(NULL, (size_t)buflen, buf);
  if (!in_mess){
    printf("Error decoding received message, printing for debug:\n");
    for(int i=0;i<buflen; i++){
      uint8_t* tempbuf = (uint8_t*) buf;
      printf(" %hhx ", tempbuf[i]);
    }
    printf("\n");
    return;
  }
  printf("ran message id %d\n", in_mess->msg_type);
  switch(in_mess->msg_type){
    case RAN_MESSAGE_TYPE__SUBSCRIPTION:
      printf("Subcription message received\n");
      handle_subscription(in_mess);
      break;
    case RAN_MESSAGE_TYPE__INDICATION_REQUEST:
      printf("Indication request message received\n");
      handle_indication_request(in_mess, out_socket, servaddr);
      break;
    case RAN_MESSAGE_TYPE__INDICATION_RESPONSE:
      printf("Indication response message received\n");
      build_indication_response(in_mess, out_socket, servaddr);
      break;
    case RAN_MESSAGE_TYPE__CONTROL:
      printf("Control message received\n");
      handle_control(in_mess);
      break;
    default:
      printf("Unrecognized message type\n");
      ran_message__free_unpacked(in_mess,NULL);
      break;
  }
}


UeListM* build_ue_list_message(){
  // init ue list protobuf message
  UeListM* ue_list_m = malloc(sizeof(UeListM));
  ue_list_m__init(ue_list_m);

  NR_UEs_t *UEs = &RC.nrmac[0]->UE_info;

  // count connected ues
  int num_ues = 0;
  while(UEs->list[num_ues] != NULL) {
    num_ues++;
  }

  // insert n ues
  ue_list_m->connected_ues = num_ues;
  ue_list_m->n_ue_info = num_ues;

  // if no ues are connected then we can stop and just return the message
  if(num_ues == 0){
    return ue_list_m;
  }

  NR_UE_info_t* curr_ue;
  NR_UE_sched_ctrl_t *sched_ctrl;
  NR_mac_stats_t *mac_stats;

  // build list of ue_info_m (this is also a protobuf message)
  UeInfoM** ue_info_list;
  ue_info_list = malloc(sizeof(UeInfoM*)*(num_ues+1)); // allocating space for 1 additional element which will be NULL (terminator element)

  for(int i = 0; i < num_ues; i++){
    // init list
    ue_info_list[i] = malloc(sizeof(UeInfoM));
    ue_info_m__init(ue_info_list[i]);

    curr_ue = UEs->list[i];

    sched_ctrl = &(curr_ue->UE_sched_ctrl);
    mac_stats = &(curr_ue->mac_stats);

    // read rnti and add to message
    ue_info_list[i]->rnti = curr_ue->rnti;

    // read measures and add to message (actually just send random data)

    // compute and add the BER
    ue_info_list[i]->has_ue_ber_downlink = 1;
    ue_info_list[i]->ue_ber_downlink = (float) (mac_stats->dl.errors) / (8 * mac_stats->dl.total_bytes);

    // add MCS downlink
    ue_info_list[i]->has_ue_mcs_downlink = 1;
    ue_info_list[i]->ue_mcs_downlink = sched_ctrl->dl_bler_stats.mcs;
  }
  // add a null terminator to the list
  ue_info_list[num_ues] = NULL;
  // assign ue info pointer to actually fill the field
  ue_list_m->ue_info = ue_info_list;
  return ue_list_m;
}

// careful, this function leaves dangling pointers - not a big deal in this case though 
void free_ue_list(UeListM* ue_list_m){
  if(ue_list_m->connected_ues > 0){
    // free the ue list content first
    int i=0;
    while(ue_list_m->ue_info[i] != NULL){ // when we reach NULL we have found the terminator (no need to free the terminator because it hasn't been allocated)
      free(ue_list_m->ue_info[i]);
      i++;
    }
    // then free the list
    free(ue_list_m->ue_info);
  }
  // finally free the outer data structure
  free(ue_list_m);
}

void ran_read(RANParameter ran_par_enum, RANParamMapEntry* map_entry){
  switch (ran_par_enum)
  {
    case RAN_PARAMETER__GNB_ID:
      map_entry->value_case=RAN_PARAM_MAP_ENTRY__VALUE_STRING_VALUE;
      map_entry->string_value = int_to_charray(gnb_id);
      break;
    case RAN_PARAMETER__UE_LIST:
      map_entry->value_case=RAN_PARAM_MAP_ENTRY__VALUE_UE_LIST;
      map_entry->ue_list = build_ue_list_message();
      break;
    default:
      printf("Unrecognized param %d\n",ran_par_enum);
      assert(0!=0);
  }
}