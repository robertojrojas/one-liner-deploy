
#include <aws/core/Aws.h>
#include <aws/ec2/EC2Client.h>
#include <aws/ec2/model/CreateVpcRequest.h>
#include <aws/ec2/model/CreateTagsRequest.h>
#include <aws/ec2/model/DescribeVpcsRequest.h>
#include <iostream>
#include <unordered_map>

#define CIDR_BLOCK "192.168.0.0/16"
#define EMPTY ""
#define NO_ERROR EMPTY
#define VPC_NAME_PREFIX  "oneliner"

const Aws::String nameResource(const Aws::EC2::EC2Client &ec2, const Aws::String& tagName, const Aws::String& resourceId) {
    Aws::EC2::Model::Tag name_tag;
    name_tag.SetKey("Name");
    name_tag.SetValue(tagName);

    Aws::EC2::Model::CreateTagsRequest create_request;
    create_request.AddResources(resourceId);
    create_request.AddTags(name_tag);

    auto create_outcome = ec2.CreateTags(create_request);
    if (!create_outcome.IsSuccess())
    {
        return create_outcome.GetError().GetMessage();
    }
    return NO_ERROR;
}

std::tuple<const Aws::String , const Aws::String>createVPC(const Aws::EC2::EC2Client &ec2) {
   Aws::EC2::Model::CreateVpcRequest createVpcReq;
   createVpcReq.SetCidrBlock(CIDR_BLOCK);

   auto createVpcOutcome = ec2.CreateVpc(createVpcReq);
   if (!createVpcOutcome.IsSuccess())
   {
      return std::make_tuple(EMPTY, createVpcOutcome.GetError().GetMessage());
   }
   auto vpcId = createVpcOutcome.GetResult().GetVpc().GetVpcId();

   std::cout << "Waiting for VPC to become available..." << std::endl;
   Aws::EC2::Model::DescribeVpcsRequest describeVpcsReq;
   Aws::Vector<Aws::String> vpcids = {vpcId};
   describeVpcsReq.SetVpcIds(vpcids);
   auto describeVpcsOutcome = ec2.DescribeVpcs(describeVpcsReq);
   if (describeVpcsOutcome.IsSuccess()) {
        while(true) {
            if (describeVpcsOutcome.GetResult().GetVpcs()[0].GetState() == Aws::EC2::Model::VpcState::available) {
                break;
            }
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
   } else {
       return std::make_tuple(EMPTY, describeVpcsOutcome.GetError().GetMessage());
   }

   auto ret = nameResource(ec2, VPC_NAME_PREFIX, vpcId);
   if (ret != NO_ERROR) {
       return std::make_tuple(EMPTY, ret);
   }
   return std::make_tuple(vpcId, EMPTY);
}


int main(int argc, char** argv) {
    Aws::SDKOptions options;
    Aws::InitAPI(options);
    {
        Aws::EC2::EC2Client ec2;
        auto result = createVPC(ec2);
        if (std::get<1>(result) != NO_ERROR) {
            std::cout << "Failed to create VPC "  <<
            std::get<1>(result) << std::endl;
            return 1;
        }
        const Aws::String& vpcId = std::get<0>(result);
        std::cout << "Created VPC: " << vpcId << std::endl;
    }    
    Aws::ShutdownAPI(options);
    return 0;
}