/*       
 * Copyright (C) 2022, Xilinx Inc - All rights reserved
 * Xilinx Resource Manger U30 Scaler Plugin 
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
#include <inttypes.h>
#include "filter-scaler-xrm-plg-u30.hpp"

/*------------------------------------------------------------------------------------------------------------------------------------------
 *
 * Populate json string input to local structure
 * ------------------------------------------------------------------------------------------------------------------------------------------*/
static int _populate_scl_data( const char* input, std::vector<ResourceData*> &m_resources, std::vector<ParamsData*> &m_params, char* child_resource)
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
      //parse optional  job-count used for launcher to overwrite load
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
             if (resource->function == "SCALER")
             {
                for (auto it2 : res_ptree.get_child("resolution.output"))
                {
                    Resolution outRes;
                    auto out_tree = it2.second;
                    outRes.width = out_tree.get<int32_t>("width");
                    outRes.height = out_tree.get<int32_t>("height");
                    outRes.frame_rate.numerator = out_tree.get<int32_t>("frame-rate.num");
                    outRes.frame_rate.denominator = out_tree.get<int32_t>("frame-rate.den");
                    resource->out_res.push_back(outRes);
                }
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


/*------------------------------------------------------------------------------------------------------------------------------------------
 *
 * XRM API version check
 * ------------------------------------------------------------------------------------------------------------------------------------------*/
int32_t xrmU30ScalPlugin_api_version(void)
{ 
  syslog(LOG_NOTICE, "%s(): API version: %d\n", __func__, XRM_API_VERSION_1);
  return (XRM_API_VERSION_1);
}

/*------------------------------------------------------------------------------------------------------------------------------------------
 *
 * XRM Plugin ID check
 * ------------------------------------------------------------------------------------------------------------------------------------------*/
int32_t xrmU30ScalPlugin_get_plugin_id(void) 
{
  syslog(LOG_NOTICE, "%s(): xrm plugin example id is %d\n", __func__, XRM_PLUGIN_U30_SCAL_ID);
  return (XRM_PLUGIN_U30_SCAL_ID); 
}

void _calc_scl_load_res( char* output, std::vector<ResourceData*> &m_resources, std::vector<ParamsData*> &m_params)
{
   syslog(LOG_NOTICE, "%s(): \n", __func__);

   int calc_percentage[MAX_OUT_ELEMENTS] = {0}, idx=0; 
   int calc_load = 0, global_calc_load = 0;
   uint64_t in_pixrate = 0;
   uint64_t ladder_pixrate = 0;
   uint64_t sess_pixrate   = 0;
   uint32_t in_frame_rate = 0, out_frame_rate=0, rounding=0;
   int calc_aggregate = 0, parse_aggregate = 0;
   char temp[1024];
   int global_job_cnt = -1;

   
   for (auto res : m_resources)
   {
       if (res->function == "SCALER")
       {
           syslog(LOG_INFO, "Multi-Scaler Session - %d Load Computation\n",idx);
           rounding = res->in_res.frame_rate.denominator>>1;
           in_frame_rate = (uint32_t)((res->in_res.frame_rate.numerator+rounding)/res->in_res.frame_rate.denominator);
           in_pixrate = (res->in_res.width * res->in_res.height * in_frame_rate);

           /* Compute processing load */
           if (res->out_res.size() > 1) {
               for(unsigned int i=0; i<res->out_res.size()-1; i++) {
                  rounding = res->out_res[i].frame_rate.denominator>>1;
                  out_frame_rate = (uint32_t)((res->out_res[i].frame_rate.numerator+rounding)/res->out_res[i].frame_rate.denominator);
                  ladder_pixrate += (res->out_res[i].width * res->out_res[i].height * out_frame_rate);
               }
               sess_pixrate = in_pixrate + ladder_pixrate;
           } else { //special case for single up-scale only
               rounding = res->out_res[0].frame_rate.denominator>>1;
               out_frame_rate = (uint32_t)((res->out_res[0].frame_rate.numerator+rounding)/res->out_res[0].frame_rate.denominator);
               ladder_pixrate += (res->out_res[0].width * res->out_res[0].height * out_frame_rate);
               sess_pixrate = ladder_pixrate;
           }
           sess_pixrate = ((in_pixrate > sess_pixrate) ? in_pixrate : sess_pixrate);

           calc_load = (long double)XRM_MAX_CU_LOAD_GRANULARITY_1000000*sess_pixrate/U30_SCAL_MAXCAPACITY;

           calc_percentage[idx] =  calc_load;

           syslog(LOG_INFO, "  in_pixrate                = %" PRIu64 "\n", in_pixrate);
           syslog(LOG_INFO, "  ladder_pixrate            = %" PRIu64 "\n", ladder_pixrate);
           syslog(LOG_INFO, "  sess_pixrate (in+ladder)  = %" PRIu64 "\n", sess_pixrate);
           syslog(LOG_INFO, "  calc_load[%d]             = %d\n", idx, calc_load);
           syslog(LOG_INFO, "  new_load[%d]              = %d\n", idx, calc_percentage[idx]);
           if (calc_percentage[idx]==0) calc_percentage[idx]=1;
           idx++;
           parse_aggregate+= (res->channel_load * XRM_MAX_CU_LOAD_GRANULARITY_1000000); 
       }
   }

   for (int p=0; p<(idx) ; p++)   
      calc_aggregate += calc_percentage[p];

   syslog(LOG_INFO, "  Aggregate Load Request    = %d\n", calc_aggregate);
   syslog(LOG_INFO, "  Aggregate Channel Load    = %d\n", parse_aggregate);



   //global  job-count used for launcher to overwrite load
   for (auto gparam : m_params)
   {     
      global_job_cnt = gparam->job_count;
      syslog(LOG_INFO, "  global_job_cnt             = %d\n",  global_job_cnt);
   }

   if (global_job_cnt > 0)
   {
      global_calc_load            =  (long double)XRM_MAX_CU_LOAD_GRANULARITY_1000000/global_job_cnt;  
      syslog(LOG_INFO, "  job_scal_calc_load        = %d\n", global_calc_load);
   }

   //Parsed job-count to be used if it limits channels to less than the calculated. 
   if ((global_calc_load > calc_aggregate) && (global_calc_load <= XRM_MAX_CU_LOAD_GRANULARITY_1000000))
      sprintf( temp,"%d ",global_calc_load);
   else
   {
      if ((parse_aggregate > calc_aggregate) && (parse_aggregate <= XRM_MAX_CU_LOAD_GRANULARITY_1000000))// Parse channel-load to be depricated by GA-2
         sprintf(temp,"%d ",parse_aggregate); 
      else
         sprintf( temp,"%d ",calc_aggregate); 
   }
   strcat(output,temp);
   sprintf( temp,"%d ",idx); 
   strcat(output,temp);
}

/*------------------------------------------------------------------------------------------------------------------------------------------
 * 
 * XRM U30 scaler plugin for load calculation
 * Expected json format string in ResourceData structure format.
 * ------------------------------------------------------------------------------------------------------------------------------------------*/
int32_t xrmU30ScalPlugin_CalcPercent(xrmPluginFuncParam* param)
{
   std::vector<ResourceData*>  m_resources;
   std::vector<ParamsData*>  m_params;
   strcpy(param->output, "");
   int nres= 0, max_nres=2, outstat=-1;

   syslog(LOG_NOTICE, "%s(): \n", __func__);

   for (nres=0; nres<2; )
   {
      if (nres==0)
         outstat = _populate_scl_data( param->input, m_resources, m_params, "resources");   
      else
         outstat = _populate_scl_data( param->input, m_resources, m_params, "additionalresources_1");   

      if (outstat == -1)
      {
         syslog(LOG_NOTICE, "%s(): failure in parsing json input\n", __func__);
         return XRM_ERROR;      
      }
      else if (outstat ==0) //No additional resources given so don't add any to param output
         return XRM_SUCCESS;    
       
      _calc_scl_load_res( param->output, m_resources, m_params);

      nres++;
   }
 
   return XRM_SUCCESS;
}

