$(document).ready(function() {

  $('body.home #commands').t(
    '<ins>1 </ins><mark>$></mark> infinit-storage --create --aws-s3 --name s3 --capacity 10TB' +
    '<ins>0.5</ins><ins>\nCreated storage "s3". \n\n</ins>' +
    '<mark>$></mark> <ins>2</ins>'+

    'infinit-network --create --storage s3 --storage local --name hybrid-cloud' +
    '<ins>0.5</ins><ins>\nCreated network "hybrid-cloud". \n\n</ins>' +
    '<mark>$></mark> <ins>2</ins>'+

    'infinit-volume --create --network hybrid-cloud --name company' +
    '<ins>0.5</ins><ins>\nStarting network "hybrid-cloud".'+
    '\nCreating volume root blocks.'+
    '\nCreated volume "company". \n\n</ins>' +
    '<mark>$></mark> <ins>2</ins>'+

    'infinit-volume --mount --name company --mountpoint /mnt/company/' +
    '<ins>0.5</ins><ins>\nMounting volume "company". \n\n</ins>' +
    '<mark>$></mark> <ins>2</ins>'+

    'ls /mnt/company/' +
    'Design/     Engineering/      Guidelines.pdf     Human Resources/     Introduction.pdf' +
    'Management/     Marketing/     Product/     Sales/     Support/     User Experience/' +

    '<mark>$></mark> ',
    {
      speed: 20,
      speed_vary: true,
      fin: function(elem) {
      }
    }
  );

});
