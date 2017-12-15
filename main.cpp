
#include <aws/core/Aws.h>
#include <aws/ec2/EC2Client.h>
#include <aws/ec2/model/CreateVpcRequest.h>
#include <aws/ec2/model/CreateTagsRequest.h>
#include <aws/ec2/model/DescribeVpcsRequest.h>
#include <aws/ec2/model/DhcpConfiguration.h>
#include <aws/ec2/model/DhcpOptions.h>
#include <aws/ec2/model/CreateDhcpOptionsRequest.h>
#include <aws/ec2/model/AssociateDhcpOptionsRequest.h>
#include <aws/ec2/model/NewDhcpConfiguration.h>
#include <iostream>
#include <unordered_map>

#define CIDR_BLOCK "192.168.0.0/16"
#define EMPTY ""
#define NO_ERROR EMPTY
#define VPC_NAME_PREFIX  "oneliner"
#define VPC_NAME "oneliner-vpc" // TODO: Figure out concat strings

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

   auto ret = nameResource(ec2, VPC_NAME, vpcId);
   if (ret != NO_ERROR) {
       return std::make_tuple(EMPTY, ret);
   }
   return std::make_tuple(vpcId, EMPTY);
}

const Aws::String dhcpOptionSets(const Aws::EC2::EC2Client &ec2, const Aws::String& vpcId) {

    Aws::EC2::Model::CreateDhcpOptionsRequest createDhcpOptionsRequest;

    Aws::EC2::Model::NewDhcpConfiguration dhcpConfec2Internal;
    dhcpConfec2Internal.SetKey("domain-name");
    Aws::Vector<Aws::String> dhcpConfec2InternalVals;
    dhcpConfec2InternalVals.push_back(Aws::String("ec2.internal"));
    dhcpConfec2Internal.SetValues(dhcpConfec2InternalVals);
    createDhcpOptionsRequest.AddDhcpConfigurations(dhcpConfec2Internal);    

    Aws::EC2::Model::NewDhcpConfiguration dhcpConfAmazonProvidedDNS;
    dhcpConfAmazonProvidedDNS.SetKey("domain-name-servers");
    Aws::Vector<Aws::String> dhcpConfAmazonProvidedDNSVals;
    dhcpConfAmazonProvidedDNSVals.push_back(Aws::String("AmazonProvidedDNS"));
    dhcpConfAmazonProvidedDNS.SetValues(dhcpConfAmazonProvidedDNSVals);
    createDhcpOptionsRequest.AddDhcpConfigurations(dhcpConfAmazonProvidedDNS); 

    std::cout << "creating DHCP Options... " << std::endl;
    auto createDhcpOptionsOutcome = ec2.CreateDhcpOptions(createDhcpOptionsRequest);
    if (!createDhcpOptionsOutcome.IsSuccess()) {
        return createDhcpOptionsOutcome.GetError().GetMessage();
    }
    auto dhcpOptionsId = createDhcpOptionsOutcome.GetResult().GetDhcpOptions().GetDhcpOptionsId();
   
    Aws::EC2::Model::AssociateDhcpOptionsRequest associateDhcpOptionsRequest;
    associateDhcpOptionsRequest.SetVpcId(vpcId);
    associateDhcpOptionsRequest.SetDhcpOptionsId(dhcpOptionsId);
    std::cout << "Associating DHCP Options... " << vpcId << std::endl;
    auto associateDhcpOptionsOutcome = ec2.AssociateDhcpOptions(associateDhcpOptionsRequest);
    if (!associateDhcpOptionsOutcome.IsSuccess()) {
        return associateDhcpOptionsOutcome.GetError().GetMessage();
    }

    std::cout << "Naming DHCP id: " << dhcpOptionsId << std::endl;
    auto ret = nameResource(ec2, "dhcp_options_onliner", dhcpOptionsId);
    if (ret != NO_ERROR) {
       return ret;
    }
    return NO_ERROR;
}


int main(int argc, char** argv) {
    Aws::SDKOptions options;
    Aws::InitAPI(options);
    {
        Aws::EC2::EC2Client ec2;
        auto vpcCreateResult = createVPC(ec2);
        if (std::get<1>(vpcCreateResult) != NO_ERROR) {
            std::cout << "Failed to create VPC "  <<
            std::get<1>(vpcCreateResult) << std::endl;
            return 1;
        }
        const Aws::String& vpcId = std::get<0>(vpcCreateResult);
        std::cout << "Created VPC: " << vpcId << std::endl;

        auto dhcpResult = dhcpOptionSets(ec2, vpcId);
        if (dhcpResult != NO_ERROR) {
            std::cout << "Failed to configure DHCP Sets "  << std::endl;
            return 1;
        }
    }    

    Aws::ShutdownAPI(options);
    return 0;
}