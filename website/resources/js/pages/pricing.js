$(document).ready(function() {

  $("#usersInput").bind("slider:changed", function (event, data) {
    console.log(data.value);
    var users = $('.estimation table .users td span');
    users.text(data.value);
  });

});