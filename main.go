package main

import (
	"encoding/base64"
	"errors"
	"fmt"
	"io/ioutil"
	"log"
	"net/http"
	"os"
	"sort"
	"strings"
	"time"

	"github.com/aws/aws-sdk-go/aws"
	"github.com/aws/aws-sdk-go/aws/session"
	"github.com/aws/aws-sdk-go/service/ec2"
)

const (
	vpcCidr            = "192.168.0.0/16"
	subnetCdirTemplate = "192.168.%d.0/24"
	vpcNamePrefix      = "oneliner"
	anywhere           = "0.0.0.0/0"
	// eu-west-1
	//ubuntuAMI = "ami-8fd760f6"
	// us-east-1
	ubuntuAMI                   = "ami-13c15e69"
	ec2Type                     = "t2.medium"
	keyName                     = "oneliner-key"
	keyPairFilename             = keyName + ".pem"
	pollTimes                   = 120
	installPythonScriptFilename = "install_python.sh"
	secGroupName                = vpcNamePrefix + "-sg"
)

func main() {

	err := checkRequiredEnvVars()
	check(err)

	ec2Svc := getEC2Client()

	vpc, err := createVPC(ec2Svc)
	check(err)

	err = dhcpOptionSets(ec2Svc, vpc)
	check(err)

	subnets, err := createSubnets(ec2Svc, vpc)
	check(err)

	igw, err := createInternetGateway(ec2Svc, vpc)
	check(err)

	err = createCustomRouteTable(ec2Svc, vpc, igw, subnets[0])
	check(err)

	sg, err := createSecurityGroup(ec2Svc, vpc)
	check(err)

	err = createSSHKeyPair(ec2Svc)
	check(err)

	instance, err := runInstance(ec2Svc, subnets[0], sg)
	check(err)

	// print '###### SAMPLE APP URL: http://{}:8000 ######'.format(pub_ip)
	pubIP, err := getInstancePublicIP(ec2Svc, instance.InstanceId)
	check(err)
	fmt.Printf("###### SAMPLE APP URL: http://%s:8000 ######\n", pubIP)

	// write_file('inventory','ol ansible_user=ubuntu ansible_ssh_host={} ansible_ssh_port=22 ansible_ssh_private_key_file=./oneliner-key.pem'.format(pub_ip))
	invetoryContents := fmt.Sprintf("ol ansible_user=ubuntu ansible_ssh_host=%s ansible_ssh_port=22 ansible_ssh_private_key_file=./%s", pubIP, keyPairFilename)
	err = writeFile("inventory", []byte(invetoryContents))
	check(err)

	// print 'CLEANUP: ./cleanup.sh {0} {1}'.format(vpc.id, ec2_instance.id)
	fmt.Printf("CLEANUP: ./cleanup.sh %s %s\n", *vpc.VpcId, *instance.InstanceId)
}

func createVPC(client *ec2.EC2) (*ec2.Vpc, error) {
	fmt.Println("creating VPC...")
	vi := &ec2.CreateVpcInput{
		CidrBlock: aws.String(vpcCidr),
	}

	vpcCreateOutput, err := client.CreateVpc(vi)
	if err != nil {
		return nil, err
	}
	dvi := &ec2.DescribeVpcsInput{
		VpcIds: []*string{vpcCreateOutput.Vpc.VpcId},
	}
	err = client.WaitUntilVpcAvailable(dvi)
	if err != nil {
		return nil, err
	}
	nameResource(client, vpcCreateOutput.Vpc.VpcId, vpcNamePrefix+"-vpc")
	if err != nil {
		return nil, err
	}
	return vpcCreateOutput.Vpc, nil
}

func dhcpOptionSets(client *ec2.EC2, vpc *ec2.Vpc) error {
	fmt.Println("creating dhcpOptionSets...")
	dhcpConfigs := []*ec2.NewDhcpConfiguration{
		&ec2.NewDhcpConfiguration{
			Key:    aws.String("domain-name"),
			Values: []*string{aws.String("ec2.internal")},
		},
		&ec2.NewDhcpConfiguration{
			Key:    aws.String("domain-name-servers"),
			Values: []*string{aws.String("AmazonProvidedDNS")},
		},
	}
	dhcpOI := &ec2.CreateDhcpOptionsInput{
		DhcpConfigurations: dhcpConfigs,
	}
	createDhcpOptionsOutput, err := client.CreateDhcpOptions(dhcpOI)
	if err != nil {
		return err
	}
	adoi := &ec2.AssociateDhcpOptionsInput{
		DhcpOptionsId: createDhcpOptionsOutput.DhcpOptions.DhcpOptionsId,
		VpcId:         vpc.VpcId,
	}
	_, err = client.AssociateDhcpOptions(adoi)
	if err != nil {
		return err
	}
	nameResource(client, createDhcpOptionsOutput.DhcpOptions.DhcpOptionsId, "dhcp_options_onliner")
	if err != nil {
		return err
	}
	return nil
}

func createSubnets(client *ec2.EC2, vpc *ec2.Vpc) ([]*string, error) {
	fmt.Println("creating createSubnets...")
	daz := &ec2.DescribeAvailabilityZonesInput{}
	azs, err := client.DescribeAvailabilityZones(daz)
	if err != nil {
		return nil, err
	}
	subnetIDs := make([]*string, 0)
	for cidrIdx, az := range azs.AvailabilityZones {
		subbnetCidr := fmt.Sprintf(subnetCdirTemplate, cidrIdx+1)
		csi := &ec2.CreateSubnetInput{
			AvailabilityZone: az.ZoneName,
			CidrBlock:        aws.String(subbnetCidr),
			VpcId:            vpc.VpcId,
		}
		createSubnetOutput, err := client.CreateSubnet(csi)
		if err != nil {
			return nil, err
		}
		subnetID := createSubnetOutput.Subnet.SubnetId
		dsi := &ec2.DescribeSubnetsInput{
			SubnetIds: []*string{subnetID},
		}
		client.WaitUntilSubnetAvailable(dsi)
		if err != nil {
			return nil, err
		}
		subnetIDs = append(subnetIDs, subnetID)
		nameResource(client, subnetID, vpcNamePrefix+"-sub-"+*az.ZoneName)
	}

	if err != nil {
		return nil, err
	}

	return subnetIDs, nil
}

func createInternetGateway(client *ec2.EC2, vpc *ec2.Vpc) (*ec2.InternetGateway, error) {
	fmt.Println("creating createInternetGateway...")
	cigi := &ec2.CreateInternetGatewayInput{}
	createInternetGatewayOutput, err := client.CreateInternetGateway(cigi)
	if err != nil {
		return nil, err
	}
	igwID := createInternetGatewayOutput.InternetGateway.InternetGatewayId
	aigi := &ec2.AttachInternetGatewayInput{
		VpcId:             vpc.VpcId,
		InternetGatewayId: igwID,
	}
	_, err = client.AttachInternetGateway(aigi)
	if err != nil {
		return nil, err
	}
	nameResource(client, igwID, vpcNamePrefix+"-igw")
	if err != nil {
		return nil, err
	}
	return createInternetGatewayOutput.InternetGateway, nil
}

func createCustomRouteTable(client *ec2.EC2, vpc *ec2.Vpc, igw *ec2.InternetGateway, subnetID *string) error {
	fmt.Println("creating createCustomRouteTable...")
	crti := &ec2.CreateRouteTableInput{
		VpcId: vpc.VpcId,
	}
	createRouteTableOutput, err := client.CreateRouteTable(crti)
	if err != nil {
		return err
	}
	cri := &ec2.CreateRouteInput{
		DestinationCidrBlock: aws.String(anywhere),
		NatGatewayId:         igw.InternetGatewayId,
		RouteTableId:         createRouteTableOutput.RouteTable.RouteTableId,
	}
	_, err = client.CreateRoute(cri)
	if err != nil {
		return err
	}

	arti := &ec2.AssociateRouteTableInput{
		RouteTableId: createRouteTableOutput.RouteTable.RouteTableId,
		SubnetId:     subnetID,
	}
	_, err = client.AssociateRouteTable(arti)
	if err != nil {
		return err
	}
	nameResource(client, createRouteTableOutput.RouteTable.RouteTableId, vpcNamePrefix+"-rt")
	if err != nil {
		return err
	}
	return nil
}

func createSecurityGroup(client *ec2.EC2, vpc *ec2.Vpc) (*string, error) {
	fmt.Println("creating createSecurityGroup...")
	csgi := &ec2.CreateSecurityGroupInput{
		GroupName:   aws.String(secGroupName),
		Description: aws.String("oneliner 8000/22"),
		VpcId:       vpc.VpcId,
	}
	createSecurityGroupOutput, err := client.CreateSecurityGroup(csgi)
	if err != nil {
		return nil, err
	}

	myIP, err := getMyIP()
	if err != nil {
		return nil, err
	}
	myIPCidr := fmt.Sprintf("%s/32", myIP)
	port := aws.Int64(8000)
	asgii8000 := &ec2.AuthorizeSecurityGroupIngressInput{
		CidrIp:     aws.String(myIPCidr),
		IpProtocol: aws.String("tcp"),
		FromPort:   port,
		ToPort:     port,
		GroupId:    createSecurityGroupOutput.GroupId,
	}
	_, err = client.AuthorizeSecurityGroupIngress(asgii8000)
	if err != nil {
		return nil, err
	}
	port = aws.Int64(22)
	asgii22 := &ec2.AuthorizeSecurityGroupIngressInput{
		CidrIp:     aws.String(myIPCidr),
		IpProtocol: aws.String("tcp"),
		FromPort:   port,
		ToPort:     port,
		GroupId:    createSecurityGroupOutput.GroupId,
	}

	_, err = client.AuthorizeSecurityGroupIngress(asgii22)
	if err != nil {
		return nil, err
	}
	nameResource(client, createSecurityGroupOutput.GroupId, "oneliner-sg")
	if err != nil {
		return nil, err
	}
	return createSecurityGroupOutput.GroupId, nil
}

func createSSHKeyPair(client *ec2.EC2) error {
	fmt.Println("creating createSSHKeyPair...")
	dkpi := &ec2.DeleteKeyPairInput{
		KeyName: aws.String(keyName),
	}
	client.DeleteKeyPair(dkpi)

	ckpi := &ec2.CreateKeyPairInput{
		KeyName: aws.String(keyName),
	}
	createKeyPairOutput, err := client.CreateKeyPair(ckpi)
	if err != nil {
		return err
	}
	writeFile(keyName+".pem", []byte(*createKeyPairOutput.KeyMaterial))
	return nil
}

func runInstance(client *ec2.EC2, subnetID *string, secGroup *string) (*ec2.Instance, error) {
	fmt.Println("creating runInstance...")
	data, err := ioutil.ReadFile(installPythonScriptFilename)
	if err != nil {
		return nil, err
	}
	installPyScript := base64.URLEncoding.EncodeToString(data)

	rii := &ec2.RunInstancesInput{
		ImageId:      aws.String(ubuntuAMI),
		InstanceType: aws.String(ec2Type),
		MinCount:     aws.Int64(1),
		MaxCount:     aws.Int64(1),
		KeyName:      aws.String(keyName),
		UserData:     aws.String(installPyScript),
		NetworkInterfaces: []*ec2.InstanceNetworkInterfaceSpecification{
			&ec2.InstanceNetworkInterfaceSpecification{
				SubnetId:                 subnetID,
				DeviceIndex:              aws.Int64(0),
				AssociatePublicIpAddress: aws.Bool(true),
				Groups: []*string{secGroup},
			},
		},
	}
	reservation, err := client.RunInstances(rii)
	if err != nil {
		return nil, err
	}
	instanceID := reservation.Instances[0].InstanceId
	disi := &ec2.DescribeInstanceStatusInput{
		InstanceIds: []*string{instanceID},
	}
	err = client.WaitUntilInstanceStatusOk(disi)
	if err != nil {
		return nil, err
	}
	nameResource(client, instanceID, "oneliner-instance")
	if err != nil {
		return nil, err
	}
	return reservation.Instances[0], nil
}

func getInstancePublicIP(client *ec2.EC2, instanceID *string) (string, error) {
	fmt.Println("creating getInstancePublicIP...")
	dii := &ec2.DescribeInstancesInput{
		InstanceIds: []*string{instanceID},
	}
	describeInstancesOutput, err := client.DescribeInstances(dii)
	if err != nil {
		return "", err
	}
	return *describeInstancesOutput.Reservations[0].Instances[0].PublicIpAddress, nil
}

func checkRequiredEnvVars() error {
	fmt.Println("creating checkRequiredEnvVars...")
	requiredEnvVars := []string{
		"AWS_DEFAULT_REGION", "AWS_ACCESS_KEY_ID", "AWS_SECRET_ACCESS_KEY",
	}
	for _, requireEnvVar := range requiredEnvVars {
		envVarValue := strings.TrimSpace(os.Getenv(requireEnvVar))
		if envVarValue == "" {
			missingVar := fmt.Sprintf("Missing environment variable %s", requireEnvVar)
			return errors.New(missingVar)
		}
	}
	return nil
}

func writeFile(filename string, contents []byte) error {
	err := ioutil.WriteFile(filename, contents, 0644)
	if err != nil {
		return err
	}
	return nil
}

func getMyIP() (string, error) {
	fmt.Println("creating getMyIP...")
	res, err := http.Get("http://ipecho.net/plain")
	if err != nil {
		return "", err
	}
	defer res.Body.Close()
	ip, err := ioutil.ReadAll(res.Body)
	if err != nil {
		return "", err
	}
	fmt.Println("creating getMyIP...", string(ip))
	return string(ip), nil
}

type amazonImage struct {
	Id           string
	CreationDate time.Time
}

type sortableamazonImage []*amazonImage

func (s sortableamazonImage) Len() int {
	return len(s)
}

func (s sortableamazonImage) Swap(i, j int) {
	s[i], s[j] = s[j], s[i]
}

func (s sortableamazonImage) Less(i, j int) bool {
	return s[i].CreationDate.After(s[j].CreationDate)
}

func findUbuntuAMI(client *ec2.EC2) (*string, error) {
	fmt.Println("creating findUbuntuAMI...")
	diiFName := "name"
	diiValue := "ubuntu/images/hvm-ssd/ubuntu-xenial-16.04-amd64*"
	diiFilter := &ec2.Filter{
		Name:   &diiFName,
		Values: []*string{&diiValue},
	}
	diiFilters := []*ec2.Filter{diiFilter}
	dii := &ec2.DescribeImagesInput{
		Filters: diiFilters,
	}
	amis, err := client.DescribeImages(dii)
	if err != nil {
		return nil, err
	}

	amisToSort := make([]*amazonImage, 0)
	for _, ami := range amis.Images {
		amiCreationDate, err := time.Parse(time.RFC3339, *ami.CreationDate)
		if err != nil {
			log.Fatal(err)
		}
		amiToAdd := &amazonImage{
			Id:           *ami.ImageId,
			CreationDate: amiCreationDate,
		}
		amisToSort = append(amisToSort, amiToAdd)
	}
	sort.Sort(sortableamazonImage(amisToSort))
	return &amisToSort[0].Id, nil
}

func nameResource(client *ec2.EC2, resourceID *string, tagName string) error {
	tag := &ec2.Tag{
		Key:   aws.String("Name"),
		Value: aws.String(tagName),
	}
	tags := []*ec2.Tag{tag}
	resources := []*string{resourceID}
	cti := &ec2.CreateTagsInput{
		Tags:      tags,
		Resources: resources,
	}
	_, err := client.CreateTags(cti)
	if err != nil {
		return err
	}
	return nil
}

func getEC2Client() *ec2.EC2 {
	fmt.Println("creating getEC2Client...")
	// Load session from shared config
	sess := session.Must(session.NewSessionWithOptions(session.Options{
		SharedConfigState: session.SharedConfigEnable,
	}))

	// Create new EC2 client
	ec2Svc := ec2.New(sess)
	return ec2Svc
}

func check(e error) {
	if e != nil {
		log.Fatal(e)
	}
}
