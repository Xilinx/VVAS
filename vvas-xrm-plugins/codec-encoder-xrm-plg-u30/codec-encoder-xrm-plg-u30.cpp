/*       
 * Copyright (C) 2022, Xilinx Inc - All rights reserved
 * Xilinx Resource Manger U30 Encoder Plugin 
 *                                    
 * Licensed under the Apache License, Version 2.0 (the "License"). You may
 * not use this file except in compliance with the License. A copy of the
 * License is located at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
 * WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. See the
 * License for the specific language governing permissions and limitations 
 * under the License.
 */        
#include "codec-encoder-xrm-plg-u30.hpp"

/*------------------------------------------------------------------------------------------------------------------------------------------
 *
 * Populate json string input to local structure
 * ------------------------------------------------------------------------------------------------------------------------------------------*/
static int _populate_enc_data( const char* input, std::vector<ResourceData*> &m_resources, std::vector<ParamsData*> &m_params, char* child_resource)
{
    syslog(LOG_NOTICE, "%s(): \n", __func__);
    stringstream job_str;
    pt::ptree job_tree;

    job_str<<input;
    boost::property_tree::read_json(job_str, job_tree);
    m_resources.clear();
    m_params.clear();

    try
    {
      //parse optional  channel-cnt used for launcher to overwrite load
      auto pr_ptree = job_tree.get_child("request.parameters");
      auto paramVal = new ParamsData;

      if (pr_ptree.not_found() != pr_ptree.find("job-count")) 
         paramVal->job_count = pr_ptree.get<int32_t>("job-count");           
      else
         paramVal->job_count = -1;      

      m_params.push_back(paramVal);

      // parse resources
      if ( pr_ptree.find(child_resource) != pr_ptree.not_found()) 
      {
         for (auto it1 : pr_ptree.get_child(child_resource))
         {
              auto res_ptree = it1.second;
              auto resource = new ResourceData;

              resource->function = res_ptree.get<std::string>("function");
              resource->format = res_ptree.get<std::string>("format");
              auto it_cl = res_ptree.find("channel-load");
              if (it_cl != res_ptree.not_found())
                 resource->channel_load = res_ptree.get<int32_t>("channel-load");
              else
                 resource->channel_load = 0;
              resource->in_res.width = res_ptree.get<int32_t>("resolution.input.width");
              resource->in_res.height = res_ptree.get<int32_t>("resolution.input.height");
              resource->in_res.frame_rate.numerator = res_ptree.get<int32_t>("resolution.input.frame-rate.num");
              resource->in_res.frame_rate.denominator = res_ptree.get<int32_t>("resolution.input.frame-rate.den");      
              if (resource->function == "ENCODER")
              {
                 auto it_la = res_ptree.find("lookahead-load");
                 if (it_la != res_ptree.not_found())
                    resource->lookahead_load = res_ptree.get<int32_t>("lookahead-load");
                 else
                    resource->lookahead_load = 0;
              }
              m_resources.push_back(resource);
         }  
      } 
      else if (strcmp(child_resource, "additionalresources_1")==0)
         return 0;
    }
    catch (std::exception &e)
    {
        syslog(LOG_NOTICE, "%s Exception: %s\n", __func__, e.what());
        return -1;
    }

    return 1;
}

static void _calc_enc_load_res( char* output, std::vector<ResourceData*> &m_resources, std::vector<ParamsData*> &m_params)
{
   syslog(LOG_NOTICE, "%s(): \n", __func__);
   uint32_t calc_enc_percentage[MAX_OUT_ELEMENTS] = {0}, calc_la_percentage[MAX_OUT_ELEMENTS] = {0}, idx=0; 
   uint32_t calc_enc_load = 0, calc_la_load = 0, global_calc_enc_load = 0, global_calc_la_load = 0;
   bool outstat;
   uint32_t rounding =0, frame_rate = 0;
   uint32_t calc_enc_aggregate = 0, parse_enc_aggregate = 0;
   uint32_t calc_la_aggregate = 0, parse_la_aggregate = 0;
   char temp1[1024], temp2[1024];
   int32_t global_job_cnt = -1, la_global_job_cnt = -1;
   uint32_t local_calc_enc_percentage = 0, local_calc_la_percentage = 0;

   for (auto res : m_resources)
   {

       if (res->function == "ENCODER")
       {
           rounding =res->in_res.frame_rate.denominator>>1;
           frame_rate = (uint32_t)((res->in_res.frame_rate.numerator+rounding)/res->in_res.frame_rate.denominator);

           calc_enc_load =  (long double)XRM_MAX_CU_LOAD_GRANULARITY_1000000* res->in_res.width*  res->in_res.height * frame_rate/U30_ENC_MAXCAPACITY;                     
           calc_la_load =  (long double)XRM_MAX_CU_LOAD_GRANULARITY_1000000* res->in_res.width*  res->in_res.height * frame_rate/U30_LA_MAXCAPACITY; 

           calc_enc_percentage[idx] = calc_enc_load;
           calc_la_percentage[idx] =  calc_la_load;

           syslog(LOG_INFO, "  in_width                 = %d\n",  res->in_res.width);
           syslog(LOG_INFO, "  in_height                = %d\n",  res->in_res.height );
           syslog(LOG_INFO, "  frame_rate               = %d\n",  frame_rate);
           syslog(LOG_INFO, "  enc_max_capacity         = %ld\n", (long int)U30_ENC_MAXCAPACITY);
           syslog(LOG_INFO, "  calc_enc_load            = %d\n",  calc_enc_load);
           syslog(LOG_INFO, "  enc_new_load[%d]         = %d\n",  idx, calc_enc_percentage[idx]);
           syslog(LOG_INFO, "  la_max_capacity          = %ld\n", (long int)U30_LA_MAXCAPACITY);
           syslog(LOG_INFO, "  calc_la_load             = %d\n",  calc_la_load);
           syslog(LOG_INFO, "  la_new_load[%d]          = %d\n",  idx, calc_la_percentage[idx]);

           if (calc_enc_percentage[idx]==0) calc_enc_percentage[idx]=1;
           if (calc_la_percentage[idx]==0) calc_la_percentage[idx]=1;

           idx++;
           parse_enc_aggregate+= (res->channel_load*10000);

       }

   }

   for (int p=0; p<(idx) ; p++)   
   {
      calc_enc_aggregate += calc_enc_percentage[p];
      calc_la_aggregate += calc_la_percentage[p];
   }


   //global  job-count used for launcher to overwrite load
   for (const auto gparam : m_params)
   {     
      global_job_cnt = gparam->job_count;
      syslog(LOG_INFO, "  Encoder global_job_cnt      = %d\n",  global_job_cnt);
   }

   if (global_job_cnt > 0)
   {
      global_calc_enc_load            =  (long double)XRM_MAX_CU_LOAD_GRANULARITY_1000000/global_job_cnt;  
      syslog(LOG_INFO, "  job_enc_calc_load    = %d\n", global_calc_enc_load);

      global_calc_la_load             =  global_calc_enc_load;
      syslog(LOG_INFO, "  job_la_calc_load       = %d\n", global_calc_la_load);
   }

   //Explicit describe job parse option for lookahead load is to be removed
   parse_la_aggregate = 2 * parse_enc_aggregate;
   
   //Parsed job-count to be used if it limits channels to less than the calculated. 
   if ((global_calc_enc_load >= calc_enc_aggregate) && (global_calc_enc_load <= XRM_MAX_CU_LOAD_GRANULARITY_1000000))
   {	   
      sprintf( temp1,"%d ",global_calc_enc_load);
      strcat(output,temp1);
      sprintf( temp1,"%d ",idx); 
      strcat(output,temp1);
      if (global_calc_la_load > XRM_MAX_CU_LOAD_GRANULARITY_1000000)
          global_calc_la_load = XRM_MAX_CU_LOAD_GRANULARITY_1000000; //exception for 4K case 	  
      sprintf(temp2,"%d ",global_calc_la_load);
      strcat(output,temp2);	  
   }	  
   else
   {
      if ((parse_enc_aggregate > calc_enc_aggregate) && (parse_enc_aggregate <= XRM_MAX_CU_LOAD_GRANULARITY_1000000))
      {		    
         sprintf( temp1,"%d ",parse_enc_aggregate); 
         strcat(output,temp1);
         sprintf( temp1,"%d ",idx); 
         strcat(output,temp1);
         if (parse_enc_aggregate > XRM_MAX_CU_LOAD_GRANULARITY_1000000)
            parse_enc_aggregate = XRM_MAX_CU_LOAD_GRANULARITY_1000000; //exception for 4K case 
		 
         sprintf( temp2,"%d ",parse_la_aggregate); 
         strcat(output,temp2);		 
      }
      else
      {	   
         sprintf( temp1,"%d ",calc_enc_aggregate);  
         strcat(output,temp1);	
         sprintf( temp1,"%d ",idx); 
         strcat(output,temp1);	
         if (calc_la_aggregate > XRM_MAX_CU_LOAD_GRANULARITY_1000000)
            calc_la_aggregate = XRM_MAX_CU_LOAD_GRANULARITY_1000000; //exception for 4K case 

         sprintf( temp2,"%d ",calc_la_aggregate); 
         strcat(output,temp2);		 
      }
   }

   syslog(LOG_INFO, "  Encoder Aggregate Load Request    = %d\n", calc_enc_aggregate);
   syslog(LOG_INFO, "  Encoder Aggregate Channel Load    = %d\n", parse_enc_aggregate);

   syslog(LOG_INFO, "  LookAhead Aggregate Load Request    = %d\n", calc_la_aggregate);
   syslog(LOG_INFO, "  LookAhead Aggregate Channel Load    = %d\n", parse_la_aggregate);

}


/*------------------------------------------------------------------------------------------------------------------------------------------
 *
 * XRM API version check
 * ------------------------------------------------------------------------------------------------------------------------------------------*/
int32_t xrmU30EncPlugin_api_version(void)
{ 
  syslog(LOG_NOTICE, "%s(): API version: %d\n", __func__, XRM_API_VERSION_1);
  return (XRM_API_VERSION_1);
}

/*------------------------------------------------------------------------------------------------------------------------------------------
 *
 * XRM Plugin ID check
 * ------------------------------------------------------------------------------------------------------------------------------------------*/
int32_t xrmU30EncPlugin_get_plugin_id(void) 
{
  syslog(LOG_NOTICE, "%s(): xrm plugin example id is %d\n", __func__, XRM_PLUGIN_U30_ENC_ID);
  return (XRM_PLUGIN_U30_ENC_ID); 
}

/*------------------------------------------------------------------------------------------------------------------------------------------
 * 
 * XRM U30 encoder plugin for load calculation
 * Expected json format string in ResourceData structure format.
 * ------------------------------------------------------------------------------------------------------------------------------------------*/
int32_t xrmU30EncPlugin_calcPercent(xrmPluginFuncParam* param)
{
   syslog(LOG_NOTICE, "%s(): \n", __func__);

   std::vector<ResourceData*>  m_resources;
   std::vector<ParamsData*>  m_params;
   strcpy(param->output, "");
   int nres= 0, max_nres=2, outstat=-1;

   for (nres=0; nres<2; )
   {

      if (nres==0)
         outstat = _populate_enc_data( param->input, m_resources, m_params, "resources");   
      else
         outstat = _populate_enc_data( param->input, m_resources, m_params, "additionalresources_1");   

      if (outstat == -1)
      {
         syslog(LOG_NOTICE, "%s(): failure in parsing json input\n", __func__);
         return XRM_ERROR;      
      }
      else if (outstat ==0) //No additional resources given so don't add any to param output
         return XRM_SUCCESS;    
       
      _calc_enc_load_res( param->output, m_resources, m_params);

      nres++;
   }

    return XRM_SUCCESS;
}


