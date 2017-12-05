#!/bin/bash


rm -f oneliner-key.pem

echo 'Provisioning AWS.... This might take a few minutes'
./main.py

if [ $? -eq 0 ]; then
  chmod 400 oneliner-key.pem
  echo "Running Ansible.... This might take a few minutes"
  PYTHONUNBUFFERED=1 ANSIBLE_FORCE_COLOR=true ANSIBLE_HOST_KEY_CHECKING=false \
      ANSIBLE_SSH_ARGS='-o UserKnownHostsFile=/dev/null -o ControlMaster=auto -o ControlPersist=60s' \
      ansible-playbook -i ./inventory ./playbook/site.yml -e 'http_port=8000' > ansible.log 2>&1
  if [ $? -eq 0 ]; then
    echo "Everyting seems OK. You should be able to navigate to the URL above. Enjoy!"
  else
    echo Ansible command failed
    cat ansible.log
  fi
else
  echo Something went wrong executing ./main.py
fi
